#!/usr/bin/env python3
"""Bundle materialization must never make writable aliases of store files."""
import argparse
import errno
import json
import hashlib
import os
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch

from nix_bundle import (build_bundle, cache_dependencies, cache_record, compiler_launcher,
                        materialize_build, pack, plan_tree, restore_bundle, restore_nar, validate_coverage_flags,
                        verified_archive, normalize_output_link)


class OwnedNarTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.cache = self.root / "cache"
        (self.cache / "nar").mkdir(parents=True)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "program").write_bytes(b"#!/bin/sh\nexit 0\n")
        (self.source / "program").chmod(0o755)
        (self.source / "asset").write_bytes(b"private asset bytes")
        (self.source / "notes.gcno").write_bytes(b"compiler notes")
        (self.source / "asset-link").symlink_to("asset")
        self.write_archive()

    def write_archive(self):
        nar = subprocess.check_output(["nix-store", "--dump", str(self.source)])
        compressed = subprocess.check_output(["zstd", "--compress", "--stdout"], input=nar)
        self.product = {"Compression": "zstd", "URL": "nar/" + "0" * 52 + ".nar.zst",
                        "FileSize": str(len(compressed)), "NarSize": str(len(nar)),
                        "FileHash": "sha256:" + hashlib.sha256(compressed).hexdigest(),
                        "NarHash": "sha256:" + hashlib.sha256(nar).hexdigest()}
        self.archive = self.cache / self.product["URL"]
        self.archive.write_bytes(compressed)

    def test_verified_owned_restore_preserves_bytes_modes_and_symlinks_without_copying(self):
        archive = verified_archive(self.cache, self.product)
        restored = self.root / "restored"
        metrics = restore_nar(archive, self.product, restored)
        self.assertEqual(int(self.product["NarSize"]), metrics["restored_nar_bytes"])
        self.assertEqual(b"#!/bin/sh\nexit 0\n", (restored / "program").read_bytes())
        self.assertEqual(b"compiler notes", (restored / "notes.gcno").read_bytes())
        self.assertTrue(os.access(restored / "program", os.X_OK))
        self.assertEqual("asset", os.readlink(restored / "asset-link"))
        before = (restored / "program").stat()
        final = self.root / "final"
        restored.rename(final)
        after = (final / "program").stat()
        self.assertEqual((before.st_dev, before.st_ino), (after.st_dev, after.st_ino))
        (final / "asset").write_bytes(b"changed privately")
        self.assertEqual(b"private asset bytes", (self.source / "asset").read_bytes())

    def test_compressed_corruption_is_rejected_before_restore(self):
        original = self.archive.read_bytes()
        self.archive.write_bytes(bytes([original[0] ^ 1]) + original[1:])
        with self.assertRaisesRegex(ValueError, "compressed bundle hash mismatch"):
            verified_archive(self.cache, self.product)

    def test_wrong_uncompressed_hash_is_rejected_after_valid_decompression(self):
        self.product["NarHash"] = "sha256:" + "0" * 64
        with self.assertRaisesRegex(ValueError, "uncompressed bundle hash/size mismatch"):
            restore_nar(self.archive, self.product, self.root / "bad-restore")

    def test_archive_paths_cannot_escape_the_downloaded_cache(self):
        (self.root / "outside.nar.zst").write_bytes(self.archive.read_bytes())
        self.product["URL"] = "../outside.nar.zst"
        with self.assertRaisesRegex(ValueError, "cache-contained"):
            verified_archive(self.cache, self.product)

    def test_dependency_closure_allows_product_self_reference_but_rejects_back_edges(self):
        product = "/nix/store/" + "0" * 32 + "-openglad-ci-coverage-1"
        dependency = "/nix/store/" + "1" * 32 + "-runtime"
        product_record = {"References": Path(product).name + " " + Path(dependency).name}
        (self.cache / ("0" * 32 + ".narinfo")).write_text(
            "StorePath: " + product + "\nReferences: " + product_record["References"] + "\n")
        info = self.cache / ("1" * 32 + ".narinfo")
        info.write_text("StorePath: " + dependency + "\nReferences: " + Path(dependency).name + "\n")
        self.assertEqual([dependency], cache_dependencies(self.cache, product, product_record))
        info.write_text("StorePath: " + dependency + "\nReferences: " + Path(product).name + "\n")
        with self.assertRaisesRegex(ValueError, "requires the product"):
            cache_dependencies(self.cache, product, product_record)
        info.write_text("StorePath: " + dependency + "\nStorePath: " + dependency + "\n")
        with self.assertRaisesRegex(ValueError, "duplicate cache metadata"):
            cache_record(self.cache, dependency)

    def prepare_consumer(self):
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.canonical = self.root / "canonical-source"
        self.destination = self.repo / "build/ci-coverage"
        self.bundle = "/nix/store/" + "0" * 32 + "-openglad-ci-coverage-1"
        self.dependency = "/nix/store/" + "1" * 32 + "-runtime"
        build = self.source / "build"
        build.mkdir()
        for path in list(self.source.iterdir()):
            if path != build:
                path.rename(build / path.name)
        (build / "CTestTestfile.cmake").write_text("command=" + str(self.canonical) + "/tests/check.py\n")
        manifest = {"format": 1, "lane": "coverage", "preset": "ci-coverage", "revision": "dirty",
                    "source": "/tmp/openglad-ci-coverage/source", "build_relative": "build/ci-coverage",
                    "source_timestamps_ns": {}, "build_timestamps_ns": {}}
        (self.source / "bundle.json").write_text(json.dumps(manifest))
        self.write_archive()
        self.product.update(StorePath=self.bundle, References=Path(self.bundle).name + " " + Path(self.dependency).name)
        (self.cache / "bundle-path.txt").write_text(self.bundle + "\n")
        (self.cache / ("0" * 32 + ".narinfo")).write_text(
            "".join(key + ": " + value + "\n" for key, value in self.product.items()))
        (self.cache / ("1" * 32 + ".narinfo")).write_text("StorePath: " + self.dependency + "\nReferences: \n")
        self.commands = []
        self.real_run = subprocess.run
        self.owned_inode = None

    def consumer_paths(self, manifest, repo):
        self.assertEqual(self.repo, repo)
        self.assertEqual("coverage", manifest["lane"])
        if self.destination.exists():
            raise ValueError("refusing to overwrite existing build tree")
        return self.canonical, self.destination

    def consumer_command(self, command, **kwargs):
        if command[:2] == ["nix", "copy"]:
            self.commands.append(command)
            return subprocess.CompletedProcess(command, 0)
        if tuple(command[:3]) == ("nix", "path-info", "--recursive"):
            return subprocess.CompletedProcess(command, 0, stdout=self.dependency + "\n")
        if command[:2] == ["nix-store", "--add-root"]:
            self.commands.append(command)
            self.assertTrue(self.destination.is_dir(), "GC roots are created only after the owned tree moves")
            return subprocess.CompletedProcess(command, 0)
        return self.real_run(command, **kwargs)

    def capture_owned_inode(self, archive, product, destination):
        result = restore_nar(archive, product, destination)
        stat = (destination / "build/program").stat()
        self.owned_inode = (stat.st_dev, stat.st_ino)
        return result

    def test_consumer_imports_only_dependencies_and_moves_owned_tree_before_rooting(self):
        self.prepare_consumer()
        with patch("nix_bundle.activation_paths", side_effect=self.consumer_paths), \
                patch("nix_bundle.subprocess.run", side_effect=self.consumer_command), \
                patch("nix_bundle.restore_nar", side_effect=self.capture_owned_inode):
            restore_bundle(argparse.Namespace(cache=self.cache, repo=self.repo))
        self.assertEqual(["nix", "copy", "--no-check-sigs", "--from", self.cache.as_uri(), self.dependency], self.commands[0])
        self.assertEqual(str(self.destination / ".nix-dependencies" / Path(self.dependency).name), self.commands[1][2])
        stat = (self.destination / "program").stat()
        self.assertEqual(self.owned_inode, (stat.st_dev, stat.st_ino))
        self.assertEqual(self.repo, self.canonical.resolve())
        self.assertEqual("command=" + str(self.repo) + "/tests/check.py\n",
                         (self.destination / "CTestTestfile.cmake").read_text())
        self.assertEqual(b"compiler notes", (self.destination / "notes.gcno").read_bytes())
        (self.destination / "asset").write_bytes(b"owned change")
        self.assertEqual(b"private asset bytes", (self.source / "build/asset").read_bytes())
        metrics = json.loads((self.destination / "bundle-activation-metrics.json").read_text())
        self.assertEqual(0, metrics["copied_bytes"])
        self.assertFalse(metrics["product_imported_by_restore"])

    def test_occupied_consumer_is_rejected_before_archive_allocation_or_import(self):
        self.prepare_consumer()
        subprocess.run(["git", "init", "--quiet", str(self.repo)], check=True)
        self.destination.mkdir(parents=True)
        def isolated_paths(*arguments, **keywords):
            if arguments == ("/tmp/openglad-ci-coverage/source",):
                return self.canonical
            return Path(*arguments, **keywords)
        # Keep the actual occupied-destination guard. Redirect only the global
        # canonical alias so an active developer bundle cannot mask its loss.
        with patch("nix_bundle.Path", side_effect=isolated_paths), \
                patch("nix_bundle.restore_nar", side_effect=self.capture_owned_inode) as restore, \
                patch("nix_bundle.subprocess.run", side_effect=self.consumer_command):
            with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
                restore_bundle(argparse.Namespace(cache=self.cache, repo=self.repo))
        restore.assert_not_called()
        self.assertEqual([], self.commands)

    def test_failed_finalization_rolls_back_owned_tree_and_new_alias(self):
        self.prepare_consumer()
        with patch("nix_bundle.activation_paths", side_effect=self.consumer_paths), \
                patch("nix_bundle.subprocess.run", side_effect=self.consumer_command), \
                patch("nix_bundle.finish_activation", side_effect=ValueError("injected finalization failure")):
            with self.assertRaisesRegex(ValueError, "injected finalization"):
                restore_bundle(argparse.Namespace(cache=self.cache, repo=self.repo))
        self.assertFalse(self.destination.exists())
        self.assertFalse(self.canonical.is_symlink())

    def test_failed_dependency_root_rolls_back_owned_tree(self):
        self.prepare_consumer()
        def fail_root(command, **kwargs):
            if command[:2] == ["nix-store", "--add-root"]:
                raise subprocess.CalledProcessError(1, command)
            return self.consumer_command(command, **kwargs)
        with patch("nix_bundle.activation_paths", side_effect=self.consumer_paths), \
                patch("nix_bundle.subprocess.run", side_effect=fail_root):
            with self.assertRaises(subprocess.CalledProcessError):
                restore_bundle(argparse.Namespace(cache=self.cache, repo=self.repo))
        self.assertFalse(self.destination.exists())
        self.assertFalse(self.canonical.is_symlink())

    def test_producer_plan_links_the_store_tree_without_copying_or_writing_it(self):
        self.prepare_consumer()
        with patch("nix_bundle.activation_paths", side_effect=self.consumer_paths), \
                patch("nix_bundle.checked_store_path", side_effect=lambda value: value), \
                patch("nix_bundle.subprocess.run", side_effect=self.consumer_command):
            plan_tree(argparse.Namespace(bundle=self.source, repo=self.repo))
        self.assertTrue(self.destination.is_symlink())
        self.assertEqual(self.source / "build", self.destination.resolve())
        self.assertEqual(self.repo, self.canonical.resolve())
        self.assertEqual(str(self.repo / "build/.plan-ci-coverage"), self.commands[0][2])
        self.assertEqual(str(self.source), self.commands[0][-1])
        self.assertEqual("command=" + str(self.canonical) + "/tests/check.py\n",
                         (self.source / "build/CTestTestfile.cmake").read_text())


class MaterializeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "store"
        self.source.mkdir()
        for name, data, mode in (("game", b"\x7fELFbinary", 0o555),
                                 ("unit.gcno", b"notes", 0o444),
                                 ("asset.dat", b"asset", 0o444),
                                 ("run.sh", b"#!/bin/sh\n", 0o555)):
            path = self.source / name
            path.write_bytes(data)
            path.chmod(mode)
        self.destination = self.root / "checkout"

    def assert_private(self, name):
        original, target = self.source / name, self.destination / name
        self.assertNotEqual(original.stat().st_ino, target.stat().st_ino)
        self.assertTrue(target.stat().st_mode & 0o200)
        target.write_bytes(b"private changes")
        self.assertNotEqual(original.read_bytes(), target.read_bytes())

    def test_compiler_launcher_captures_only_fixed_flags_as_distinct_arguments(self):
        with patch.dict("os.environ", {"NIX_CFLAGS_COMPILE": "-isystem /nix/store/dependency/include",
                "NIX_LDFLAGS": "-rpath /nix/store/dependency/lib", "NIX_HARDENING_ENABLE": "",
                "UNRELATED_VALUE": "must not be captured"}, clear=True):
            arguments = compiler_launcher(Path("/nix/store/coreutils/bin/env")).split(";")
        self.assertEqual(["/nix/store/coreutils/bin/env",
            "NIX_CFLAGS_COMPILE=-isystem /nix/store/dependency/include",
            "NIX_LDFLAGS=-rpath /nix/store/dependency/lib", "NIX_HARDENING_ENABLE="], arguments)

    def test_compiler_launcher_rejects_cmake_list_injection(self):
        for invalid in ("value;extra-command", "value\nextra-command"):
            with patch.dict("os.environ", {"NIX_CFLAGS_COMPILE": invalid}, clear=True):
                with self.assertRaisesRegex(ValueError, "CMake list"):
                    compiler_launcher(Path("/nix/store/coreutils/bin/env"))

    def test_compiler_launcher_chains_pinned_sccache_after_producer_flags(self):
        with patch.dict("os.environ", {"NIX_CFLAGS_COMPILE": "-isystem /nix/store/include"}, clear=True):
            arguments = compiler_launcher(
                Path("/nix/store/coreutils/bin/env"),
                Path("/nix/store/sccache/bin/sccache"),
                Path("/build/openglad-sccache"),
            ).split(";")
        self.assertEqual("SCCACHE_DIR=/build/openglad-sccache", arguments[-2])
        self.assertEqual("/nix/store/sccache/bin/sccache", arguments[-1])

    def test_compiler_launcher_can_key_coverage_cache_by_output(self):
        with patch.dict("os.environ", {}, clear=True):
            arguments = compiler_launcher(
                Path("/nix/store/coreutils/bin/env"),
                Path("/nix/store/sccache/bin/sccache"),
                Path("/build/openglad-sccache"),
                cache_key_shell=Path("/nix/store/bash/bin/bash"),
                cache_key_wrapper=Path("/tmp/openglad-ci-coverage/source/scripts/ci/sccache_output_key.sh"),
            ).split(";")
        self.assertEqual([
            "/nix/store/bash/bin/bash",
            "/tmp/openglad-ci-coverage/source/scripts/ci/sccache_output_key.sh",
            "/nix/store/sccache/bin/sccache",
        ], arguments[-3:])

    def test_compiler_launcher_rejects_partial_or_unpinned_cache_configuration(self):
        with self.assertRaisesRegex(ValueError, "supplied together"):
            compiler_launcher(Path("/nix/store/coreutils/bin/env"),
                              Path("/nix/store/sccache/bin/sccache"), None)
        with self.assertRaisesRegex(ValueError, "pinned store sccache"):
            compiler_launcher(Path("/nix/store/coreutils/bin/env"), Path("/usr/bin/sccache"),
                              Path("/build/openglad-sccache"))
        with self.assertRaisesRegex(ValueError, "supplied together"):
            compiler_launcher(Path("/nix/store/coreutils/bin/env"),
                              Path("/nix/store/sccache/bin/sccache"),
                              Path("/build/openglad-sccache"),
                              cache_key_shell=Path("/nix/store/bash/bin/bash"))
        with self.assertRaisesRegex(ValueError, "requires a compiler cache"):
            compiler_launcher(Path("/nix/store/coreutils/bin/env"),
                              cache_key_shell=Path("/nix/store/bash/bin/bash"),
                              cache_key_wrapper=Path("/tmp/wrapper"))

    def test_coverage_cache_key_wrapper_preserves_arguments_and_keys_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            capture = root / "sccache"
            capture.write_text(
                "#!/usr/bin/env bash\n"
                "printf '%s\\n' \"$SCCACHE_C_CUSTOM_CACHE_BUSTER\"\n"
                "printf '<%s>\\n' \"$@\"\n"
            )
            capture.chmod(0o755)
            wrapper = Path(__file__).with_name("sccache_output_key.sh")
            command = [str(wrapper), str(capture), "/nix/store/compiler/bin/g++",
                       "-DVALUE=a b", "-c", "source.cpp", "-o", "CMakeFiles/game.dir/source.cpp.o"]
            output = subprocess.check_output(command, text=True).splitlines()
            self.assertEqual("openglad-coverage-output-v1:CMakeFiles/game.dir/source.cpp.o", output[0])
            self.assertEqual([
                "</nix/store/compiler/bin/g++>", "<-DVALUE=a b>", "<-c>", "<source.cpp>",
                "<-o>", "<CMakeFiles/game.dir/source.cpp.o>",
            ], output[1:])
            glued = subprocess.check_output(
                [str(wrapper), str(capture), "cc", "-c", "source.c", "-oobject.o"],
                text=True).splitlines()
            self.assertEqual("openglad-coverage-output-v1:object.o", glued[0])
            with self.assertRaises(subprocess.CalledProcessError):
                subprocess.run([str(wrapper), str(capture), "cc", "-c", "source.c"], check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def test_local_build_preserves_history_and_replaces_the_output_link_itself(self):
        repo = self.root / 'checkout ${literal}'
        repo.mkdir()
        output = self.root / "bundle"
        output.symlink_to(self.source, target_is_directory=True)
        revision = "a" * 40
        with patch("nix_bundle.run", side_effect=["false", "325", revision]), \
                patch("nix_bundle.subprocess.run") as execute, \
                patch("nix_bundle.normalize_output_link"):
            build_bundle(argparse.Namespace(repo=repo, lane="coverage", out_link=output,
                                             sccache_seed=None, compile_shards=0,
                                             compile_shard=-1, cache_only=False, cores=3))
        command = execute.call_args.args[0]
        self.assertEqual(str(output), command[command.index("--out-link") + 1])
        self.assertEqual(self.source, output.resolve())
        expression = command[command.index("--expr") + 1]
        self.assertIn('ciCommitCount = "325"', expression)
        self.assertIn('ciGitHash = "aaaaaaaa"', expression)
        self.assertIn("git+file://", expression)
        self.assertIn("%24%7Bliteral%7D", expression)
        self.assertNotIn("${literal}", expression)
        self.assertEqual("3", command[command.index("--cores") + 1])
        self.assertEqual("1", command[command.index("--max-jobs") + 1])

    def test_local_compile_shard_is_an_explicit_seeded_sccache_output(self):
        repo = self.root / "checkout"
        repo.mkdir()
        seed = self.root / "seed"
        (seed / "cache").mkdir(parents=True)
        revision = "b" * 40
        with patch("nix_bundle.run", side_effect=["false", "326", revision]), \
                patch("nix_bundle.subprocess.run") as execute, \
                patch("nix_bundle.normalize_output_link"):
            build_bundle(argparse.Namespace(
                repo=repo, lane="asan", out_link=self.root / "cache-output",
                sccache_seed=seed, compile_shards=8, compile_shard=3,
                cache_only=True, cores=4))
        command = execute.call_args.args[0]
        expression = command[command.index("--expr") + 1]
        self.assertIn("builtins.path", expression)
        self.assertIn('ciCompileShards = "8"', expression)
        self.assertIn('ciCompileShard = "3"', expression)
        self.assertTrue(expression.endswith(").sccache"))
        self.assertEqual("4", command[command.index("--cores") + 1])

    def test_selected_sccache_output_uses_requested_link_name(self):
        requested = self.root / "cache-output"
        secondary = Path(str(requested) + "-sccache")
        secondary.symlink_to(self.source, target_is_directory=True)
        normalize_output_link(requested, True)
        self.assertEqual(self.source, requested.resolve())
        self.assertFalse(secondary.is_symlink())

    def test_local_build_rejects_incomplete_history_before_nix_runs(self):
        def git_metadata(*arguments, **kwargs):
            del kwargs
            if arguments == ("git", "rev-parse", "--is-shallow-repository"):
                return "true"
            if arguments == ("git", "rev-list", "--count", "HEAD"):
                return "325"
            if arguments == ("git", "rev-parse", "HEAD"):
                return "a" * 40
            raise AssertionError(f"unexpected metadata command: {arguments}")

        with patch("nix_bundle.run", side_effect=git_metadata), \
                patch("nix_bundle.subprocess.run") as execute:
            with self.assertRaisesRegex(ValueError, "complete Git history"):
                build_bundle(argparse.Namespace(repo=self.root, lane="coverage",
                                                 out_link=self.root / "bundle"))
        execute.assert_not_called()

    def test_coverage_rejects_a_later_optimization_override(self):
        database = self.source / "compile_commands.json"
        database.write_text(json.dumps([{"file": "tests/inline_header.cpp",
            "arguments": ["c++", "-O0", "--coverage", "-O1"]}]))
        with self.assertRaisesRegex(ValueError, "not explicitly O0"):
            validate_coverage_flags(self.source)
        database.write_text(json.dumps([{"file": "tests/inline_header.cpp",
            "arguments": ["c++", "-O1", "--coverage", "-O0", "-fprofile-update=atomic"]}]))
        validate_coverage_flags(self.source)

    def test_coverage_rejects_non_atomic_counter_overrides(self):
        database = self.source / "compile_commands.json"
        database.write_text(json.dumps([{"file": "src/threaded.cpp",
            "arguments": ["c++", "--coverage", "-O0",
                          "-fprofile-update=atomic", "-fprofile-update=single"]}]))
        with self.assertRaisesRegex(ValueError, "atomic counters"):
            validate_coverage_flags(self.source)

    def test_pack_consumes_build_without_copying_notes_or_executables_twice(self):
        source = self.root / "source"
        build = source / "build/ci-coverage"
        build.mkdir(parents=True)
        for name in ("game", "unit.gcno", "asset.dat", "unit.o", "libgame.a", "unit.gcda"):
            (build / name).write_text(name)
        (build / "compile_commands.json").write_text(json.dumps([{
            "file": "src/game.cpp", "arguments": ["c++", "--coverage", "-O0", "-fprofile-update=atomic"]}]))
        output = self.root / "bundle"
        pack(argparse.Namespace(source=source, build=build, output=output,
                                lane="coverage", revision="dirty"))
        self.assertFalse(build.exists())
        self.assertEqual({"game", "unit.gcno", "asset.dat", "compile_commands.json"},
                         {path.name for path in (output / "build").iterdir()})
        self.assertEqual("unit.gcno", (output / "build/unit.gcno").read_text())
        self.assertEqual("game", (output / "build/game").read_text())

    def test_shares_only_readonly_code_and_notes_and_keeps_assets_private(self):
        metrics = materialize_build(self.source, self.destination, immutable=True)
        self.assertEqual(2, metrics["hardlinked_files"])
        self.assertEqual(len(b"\x7fELFbinary") + len(b"notes"), metrics["hardlinked_bytes"])
        self.assertEqual(2, metrics["copied_files"])
        self.assertEqual(len(b"asset") + len(b"#!/bin/sh\n"), metrics["copied_bytes"])
        for name in ("game", "unit.gcno"):
            original, target = self.source / name, self.destination / name
            self.assertEqual(original.stat().st_ino, target.stat().st_ino)
            self.assertEqual(0, target.stat().st_mode & 0o222)
        self.assert_private("asset.dat")
        self.assert_private("run.sh")
        # Profiles go beside immutable notes, in a writable directory.
        (self.destination / "unit.gcda").write_bytes(b"fresh hits")
        self.assertFalse((self.source / "unit.gcda").exists())

    def test_canary_has_no_shared_inodes_for_incremental_relinks(self):
        metrics = materialize_build(self.source, self.destination, immutable=False)
        self.assertEqual(0, metrics["hardlinked_files"])
        for name in ("game", "unit.gcno", "asset.dat", "run.sh"):
            self.assert_private(name)

    def test_cross_filesystem_falls_back_to_private_copies(self):
        with patch("nix_bundle.os.link", side_effect=OSError(errno.EXDEV, "different devices")):
            metrics = materialize_build(self.source, self.destination, immutable=True)
        self.assertEqual(0, metrics["hardlinked_files"])
        self.assert_private("game")
        self.assert_private("unit.gcno")


if __name__ == "__main__":
    unittest.main()
