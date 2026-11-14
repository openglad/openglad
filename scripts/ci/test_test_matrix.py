#!/usr/bin/env python3
"""The scheduler must lose neither tests nor failed/missing evidence."""
import copy
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
import unittest
from unittest import mock
from types import SimpleNamespace

import test_matrix as matrix


def counter_file(*functions, summaries=1, end=True, magic=0x67636461):
    """A GCC counter file: each function is a list of counters, an int (that
    many all-zero counters, stored as a negative length), or None (announced
    here, emitted by another object)."""
    data = struct.pack('=4I', magic, 0x42353052, 7, 9)
    for _ in range(summaries):
        data += struct.pack('=IiII', 0xA1000000, 8, 1, 0)
    for function in functions:
        if function is None:
            data += struct.pack('=Ii', 0x01000000, 0)
            continue
        data += struct.pack('=Ii3I', 0x01000000, 12, 1, 2, 3)
        if isinstance(function, int):
            data += struct.pack('=Ii', 0x01A10000, -8 * function)
        else:
            data += struct.pack(f'=Ii{len(function)}q', 0x01A10000, 8 * len(function), *function)
    return data + (b'\0\0\0\0' if end else b'')


def one_function_counters(path):
    """Decode a counter_file([...]) written with exactly one counted function."""
    data = path.read_bytes()
    length = struct.unpack_from('=i', data, 16 + 16 + 20 + 4)[0]
    return list(struct.unpack_from(f'={length // 8}q', data, 16 + 16 + 20 + 8))


def summing_gcov_tool(command, **kwargs):
    """What `gcov-tool merge -o OUT A B` does to one-function counter files."""
    assert command[:3] == ['gcov-tool', 'merge', '-o'], command
    target, first, second = (Path(value) for value in command[3:])
    names = {str(path.relative_to(root)) for root in (first, second)
             for path in root.rglob('*.gcda')}
    for name in names:
        present = [root / name for root in (first, second) if (root / name).is_file()]
        counters = [sum(values) for values in zip(*map(one_function_counters, present))]
        (target / name).parent.mkdir(parents=True, exist_ok=True)
        (target / name).write_bytes(counter_file(counters))
    return subprocess.CompletedProcess(command, 0, stdout='')


class MatrixTests(unittest.TestCase):
    def inventory(self):
        return {'tests': [
            {'name': 'setup', 'properties': [{'name': 'FIXTURES_SETUP', 'value': ['arena']}]},
            {'name': 'parity', 'properties': [{'name': 'FIXTURES_REQUIRED', 'value': ['arena']}]},
            {'name': 'cleanup', 'properties': [{'name': 'FIXTURES_CLEANUP', 'value': ['arena']}]},
            {'name': 'slow'}, {'name': 'new_test'}, {'name': 'emscripten_build_test'}]}

    def plan(self):
        return matrix.partition(self.inventory(), {'seconds': {'slow': 100}}, 2, 'coverage')

    def reports(self, plan):
        return [{'shard': s['id'], 'plan_sha256': matrix.fingerprint(plan), 'exit_code': 0,
                 'results': {n: {'passed': True, 'seconds': 1} for n in s['tests']}}
                for s in plan['shards']]

    def test_complete_disjoint_and_new_test_never_dropped(self):
        plan = self.plan()
        names = [n for s in plan['shards'] for n in s['tests']]
        self.assertEqual(sorted(names), ['cleanup', 'new_test', 'parity', 'setup', 'slow'])
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(matrix.aggregate(plan, self.reports(plan))['tests'], 5)

    def test_fixtures_share_shard_and_heavy_job_balances(self):
        plan = matrix.partition(self.inventory(), {'seconds': {
            'parity': 100, 'slow': 90, 'new_test': 80, 'setup': 1, 'cleanup': 1}},
            3, 'coverage')
        groups = [set(s['tests']) for s in plan['shards']]
        self.assertEqual(groups, [{'setup', 'parity', 'cleanup'}, {'slow'}, {'new_test'}])
        self.assertEqual([s['estimated_seconds'] for s in plan['shards']], [102, 90, 80])

    def test_processor_reservations_balance_runner_time(self):
        ordinary = [{'name': f'ordinary_{index}'} for index in range(4)]
        timings = {'seconds': {'heavy': 100, **{
            test['name']: 100 for test in ordinary}}}
        for reservation in (
                {'name': 'PROCESSORS', 'value': 4},
                {'name': 'RUN_SERIAL', 'value': True}):
            inventory = {'tests': [
                {'name': 'heavy', 'properties': [reservation]}, *ordinary,
                {'name': 'emscripten_build_test'}]}
            with self.subTest(reservation=reservation):
                plan = matrix.partition(inventory, timings, 2, 'coverage')
                self.assertEqual([s['tests'] for s in plan['shards']], [
                    ['heavy'],
                    ['ordinary_0', 'ordinary_1', 'ordinary_2', 'ordinary_3']])
                self.assertEqual(
                    [s['estimated_runner_seconds'] for s in plan['shards']],
                    [100, 100])
                self.assertEqual(plan['parallel'], 4)

    def test_instrumentation_lane_timings_balance_that_lane_and_keep_fallback(self):
        inventory = {'tests': [{'name': name} for name in ('a', 'b', 'c', 'd')]}
        timings = {'seconds': {'a': 100, 'b': 90, 'c': 20, 'd': 10},
                   'lane_seconds': {'asan': {'a': 1, 'b': 1, 'c': 90, 'd': 100}}}
        coverage = matrix.partition(inventory, timings, 2, 'coverage')
        asan = matrix.partition(inventory, timings, 2, 'asan')
        self.assertEqual([s['tests'] for s in coverage['shards']], [['a', 'd'], ['b', 'c']])
        self.assertEqual([s['estimated_seconds'] for s in coverage['shards']], [110, 110])
        self.assertEqual([s['tests'] for s in asan['shards']], [['d'], ['a', 'b', 'c']])
        self.assertEqual([s['estimated_seconds'] for s in asan['shards']], [100, 92])
        # A newly recorded partial lane keeps the established cost for its other entries.
        timings['lane_seconds']['asan'] = {'a': 101}
        partial = matrix.partition(inventory, timings, 2, 'asan')
        self.assertEqual([s['estimated_seconds'] for s in partial['shards']], [111, 110])
        self.assertEqual(sorted(n for s in partial['shards'] for n in s['tests']),
                         ['a', 'b', 'c', 'd'])

    def test_missing_duplicate_wrong_plan_skipped_failed_incomplete_all_fail(self):
        plan = self.plan()
        original = self.reports(plan)
        malformed = [[], original[:1], original + original[:1]]
        for change in ('plan', 'skip', 'exit', 'missing', 'extra'):
            reports = copy.deepcopy(original)
            report = reports[0]
            name = next(iter(report['results']))
            if change == 'plan': report['plan_sha256'] = 'old'
            if change == 'skip': report['results'][name]['passed'] = False
            if change == 'exit': report['exit_code'] = 8
            if change == 'missing': del report['results'][name]
            if change == 'extra': report['results']['unplanned'] = {'passed': True}
            malformed.append(reports)
        for reports in malformed:
            with self.subTest(reports=reports), self.assertRaises(ValueError):
                matrix.aggregate(plan, reports)

    def test_junit_distinguishes_fail_skip_and_pass(self):
        with tempfile.TemporaryDirectory() as temp:
            xml = Path(temp) / 'tests.xml'
            xml.write_text('<testsuite><testcase name="pass" time="2"/>'
                           '<testcase name="fail"><failure/></testcase>'
                           '<testcase name="skip"><skipped/></testcase></testsuite>')
            self.assertEqual(matrix.junit_results(xml), {
                'pass': {'passed': True, 'seconds': 2},
                'fail': {'passed': False, 'seconds': 0},
                'skip': {'passed': False, 'seconds': 0}})

    def test_junit_rejects_hidden_internal_skips_and_reports_documented_optins(self):
        with tempfile.TemporaryDirectory() as temporary:
            xml = Path(temporary) / 'ctest.xml'
            xml.write_text('<testsuite><testcase name="native" time="1"><system-out>'
                           '[  SKIPPED ] ImaginationsTwitchers.no_living_jams_against_the_moat (0 ms)\n'
                           '[  SKIPPED ] ImaginationsTwitchers.no_living_jams_against_the_moat\n'
                           '</system-out></testcase></testsuite>')
            report = matrix.junit_results(xml)['native']
            self.assertFalse(report['passed'])
            original_xml = xml.read_text()
            xml.write_text('<testsuite><testcase name="native"><system-out>'
                           '[This part of the test output was removed since it exceeds the threshold of 1024 bytes.]'
                           '</system-out></testcase></testsuite>')
            with self.assertRaisesRegex(ValueError, 'truncated native test evidence'):
                matrix.junit_results(xml)
            xml.write_text(original_xml)
            self.assertEqual(report['unapproved_skips'],
                             ['ImaginationsTwitchers.no_living_jams_against_the_moat'])
            xml.write_text('<testsuite><testcase name="native"><system-out>'
                           '[  SKIPPED ] RenderEffects.zz_bench_large_canvas_redraw (0 ms)\n'
                           '</system-out></testcase></testsuite>')
            report = matrix.junit_results(xml)['native']
            self.assertTrue(report['passed'])
            self.assertEqual(report['unapproved_skips'], [])
            self.assertEqual(report['scheduled_skips'], [{
                'test': 'RenderEffects.zz_bench_large_canvas_redraw',
                'reason': 'opt-in OG_BENCH performance benchmark'}])
            plan = self.plan()
            reports = self.reports(plan)
            name = next(iter(reports[0]['results']))
            reports[0]['results'][name] = report
            summary = matrix.aggregate(plan, reports)
            self.assertEqual(summary['scheduled_skips'], [dict(ctest=name, **report['scheduled_skips'][0])])
            reports[0]['results'][name] = dict(passed=True, unapproved_skips=['Parity.missing_golden'])
            with self.assertRaisesRegex(ValueError, 'skipped mandatory GoogleTest'):
                matrix.aggregate(plan, reports)

    def test_worker_keeps_full_junit_output_and_rejects_a_successful_process_hidden_skip(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = self.plan()
            plan['revision'] = 'head'
            plan_file = root / 'plan.json'
            matrix.write(plan_file, plan)
            args = SimpleNamespace(plan=plan_file, shard=1, out=root / 'result',
                                   build=root / 'build', parallel=plan['parallel'])
            def spawn(command, **kwargs):
                output = ['<testsuite>']
                for name in plan['shards'][0]['tests']:
                    output.append(f'<testcase name="{name}"><system-out>'
                                  '[  SKIPPED ] Version.minor_matches_history\n'
                                  '</system-out></testcase>')
                (args.out / 'ctest.xml').write_text(''.join(output) + '</testsuite>')
                return SimpleNamespace(stdout=iter([]), wait=lambda: 0)
            inherited = {'GTEST_FILTER': 'Nothing.selected', 'GTEST_TOTAL_SHARDS': '99',
                         'GTEST_SHARD_INDEX': '98', 'GTEST_REPEAT': '-1', 'GTEST_FLAGFILE': 'unread',
                         'GTEST_FUTURE_SELECTION_FLAG': 'unknown'}
            with mock.patch.dict(matrix.os.environ, inherited), \
                 mock.patch.object(matrix.subprocess, 'check_output', side_effect=['head', '42', '12345678']), \
                 mock.patch.object(matrix.subprocess, 'Popen', side_effect=spawn) as execute, \
                 self.assertRaisesRegex(ValueError, 'skipped mandatory GoogleTest'):
                matrix.run(args)
            command = execute.call_args.args[0]
            environment = execute.call_args.kwargs['env']
            self.assertFalse(set(inherited) & environment.keys())
            self.assertEqual(environment['GTEST_FAIL_IF_NO_TEST_SELECTED'], '1')
            for flag in ('--test-output-size-passed', '--test-output-size-failed'):
                self.assertIn(flag, command)
                self.assertEqual(command[command.index(flag)+1], '0')
            report = matrix.read(args.out / 'result.json')
            self.assertEqual(report['exit_code'], 0)  # Raw CTest success is retained.
            self.assertTrue(all(not result['passed'] for result in report['results'].values()))
            self.assertTrue(all(result['unapproved_skips'] == ['Version.minor_matches_history']
                                for result in report['results'].values()))

    def test_gtest_protocol_rejects_missing_duplicate_indices_filters_and_repeats(self):
        tests = [{'name': f'group_{index}',
                  'command': ['python3', 'isolate.py', 'source', '/build/group', '--gtest_filter=*'],
                  'properties': [{'name': 'ENVIRONMENT', 'value': [
                      'GTEST_TOTAL_SHARDS=4', f'GTEST_SHARD_INDEX={index}']}]} for index in range(4)]
        inventory = {'tests': tests}
        self.assertEqual(matrix.gtest_partitions(inventory, 'asan'), [{
            'binary': 'group', 'filter': '*', 'partitions': 4,
            'entries': ['group_0', 'group_1', 'group_2', 'group_3']}])
        malformed = [dict(tests=tests[:-1]), dict(tests=tests+tests[:1])]
        for problem in ('index', 'count', 'filter', 'repeat'):
            candidate = copy.deepcopy(inventory)
            test = candidate['tests'][0]
            if problem == 'index': test['properties'][0]['value'][1] = 'GTEST_SHARD_INDEX=1'
            if problem == 'count': test['properties'][0]['value'][0] = 'GTEST_TOTAL_SHARDS=3'
            if problem == 'filter': test['command'].pop()
            if problem == 'repeat': test['properties'][0]['value'].append('GTEST_REPEAT=-1')
            malformed.append(candidate)
        for candidate in malformed:
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                matrix.gtest_partitions(candidate, 'coverage')
        # Focused TSan does not require binaries excluded from its build graph.
        self.assertEqual(matrix.gtest_partitions(inventory, 'tsan'), [])

    def test_raw_lua_dumps_flatten_without_overwrite_and_share_identical_sidecars(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, target = root / 'source', root / 'target'
            (source / 'sources').mkdir(parents=True)
            target.mkdir()
            (source / 'lua-same.luacov').write_text('process evidence')
            (source / 'sources/hash.lua').write_text('return 1')
            matrix.merge_lua_evidence(source, target, 1)
            matrix.merge_lua_evidence(source, target, 2)
            self.assertEqual(sorted(p.name for p in target.glob('*.luacov')),
                             ['shard-1-lua-same.luacov', 'shard-2-lua-same.luacov'])
            self.assertEqual((target / 'sources/hash.lua').read_text(), 'return 1')
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                matrix.merge_lua_evidence(source, target, 1)
            (source / 'sources/hash.lua').write_text('different content')
            with self.assertRaisesRegex(ValueError, 'conflicting'):
                matrix.merge_lua_evidence(source, target, 3)

    def test_object_partitions_cover_every_note_once_and_never_mix_variants(self):
        notes = ([f'CMakeFiles/og_core.dir/src/core/a{i}.cpp.gcno' for i in range(5)]
                 + [f'tests/CMakeFiles/og_unit_x.dir/tests/unit/t{i}.cpp.gcno' for i in range(3)]
                 + [f'CMakeFiles/og_core_test.dir/src/core/a{i}.cpp.gcno' for i in range(6)]
                 + ['CMakeFiles/og_game_test.dir/src/x.cpp.gcno'])
        exclusions = [r'(?:.*/)?CMakeFiles/og_core_test\.dir(?:/|$)',
                      r'(?:.*/)?CMakeFiles/og_game_test\.dir(?:/|$)']
        for workers in (1, 2, 4, 8):
            with self.subTest(workers=workers):
                jobs = matrix.object_partitions(notes, exclusions, workers)
                self.assertLessEqual(len(jobs), max(workers, 2))
                directories = [d for job in jobs for d in job['directories']]
                self.assertEqual(sorted(directories), [
                    'CMakeFiles/og_core.dir', 'CMakeFiles/og_core_test.dir',
                    'CMakeFiles/og_game_test.dir', 'tests/CMakeFiles/og_unit_x.dir'])
                variants = {d: job['variant'] for job in jobs for d in job['directories']}
                self.assertEqual(variants, {
                    'CMakeFiles/og_core.dir': 'production',
                    'tests/CMakeFiles/og_unit_x.dir': 'production',
                    'CMakeFiles/og_core_test.dir': 'testing',
                    'CMakeFiles/og_game_test.dir': 'testing'})
        # A note the split cannot place would be a silently smaller inventory.
        with self.assertRaisesRegex(ValueError, 'outside a CMake target directory'):
            matrix.object_partitions(['stray.gcno'], exclusions, 4)
        # One population missing means the variant split described nothing.
        with self.assertRaisesRegex(ValueError, 'lacks a production or a TESTING'):
            matrix.object_partitions(notes[:8], exclusions, 4)
        with self.assertRaisesRegex(ValueError, 'lacks a production or a TESTING'):
            matrix.object_partitions(notes[8:], exclusions, 4)
        with self.assertRaisesRegex(ValueError, 'at least one worker'):
            matrix.object_partitions(notes, exclusions, 0)

    def test_counter_file_totals_count_and_sum_every_arc_counter(self):
        self.assertEqual((0, 0), matrix.counter_file_totals(counter_file(), 'empty.gcda'))
        self.assertEqual((3, 60), matrix.counter_file_totals(
            counter_file([10, 20, 30]), 'one.gcda'))
        # A zero run has no payload, and a function emitted elsewhere has no counters.
        self.assertEqual((9, 7), matrix.counter_file_totals(
            counter_file([1, 2], 4, None, [4, 0, 0]), 'mixed.gcda'))
        self.assertEqual((1, (1 << 63) - 1), matrix.counter_file_totals(
            counter_file([(1 << 63) - 1]), 'largest.gcda'))
        # libgcov's closing zero word is optional.
        self.assertEqual((1, 5), matrix.counter_file_totals(
            counter_file([5], end=False), 'unterminated.gcda'))

    def test_counter_file_totals_refuse_negative_counters_and_anything_unrecognised(self):
        good = counter_file([10, 20, 30])
        refused = {
            'negative or overflowed': counter_file([10, -1, 30]),
            'negative or overflowed ': counter_file([1], [-(1 << 63)]),
            'not a GCC counter file': counter_file([1], magic=0x67636E6F),  # a .gcno
            'not a GCC counter file ': b'gcda',
            'exactly one summary': counter_file([1], summaries=0),
            'exactly one summary ': counter_file([1], summaries=2),
            'truncated': good[:-9],
            'truncated ': good[:-4] + b'\0\0',
            'misplaced or misaligned': (counter_file()[:-4]
                                        + struct.pack('=Iiq', 0x01A10000, 8, 1)),
            'misplaced or misaligned ': (counter_file([1])[:-4 - 16]
                                         + struct.pack('=Ii', 0x01A10000, 12) + b'\0' * 12),
            # A value-profile counter kind: this build must not contain one.
            'unknown record': counter_file([1])[:-4] + struct.pack('=Iiq', 0x01A30000, 8, 1),
        }
        for message, data in refused.items():
            with self.subTest(message=message), \
                    self.assertRaisesRegex(ValueError, message.strip()):
                matrix.counter_file_totals(data, 'bad.gcda')

    def test_worker_collection_ships_every_counter_file_and_nothing_else(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / 'build'
            hit = 'CMakeFiles/product.dir/src/a.cpp'
            cold = 'CMakeFiles/product.dir/src/b.cpp'
            (build / hit).parent.mkdir(parents=True)
            for name in (hit, cold):
                (build / (name + '.gcno')).write_bytes(b'note ' + name.encode())
            (build / (hit + '.gcda')).write_bytes(counter_file([2, 3]))
            (build / 'game').write_bytes(b'executable')
            notes = matrix.compiler_notes(build)
            args = SimpleNamespace(build=build, out=root / 'results')
            with mock.patch.object(matrix.subprocess, 'run') as execute:
                matrix.collect(args)
            execute.assert_not_called()  # a worker neither scans nor formats
            self.assertEqual(notes, matrix.read(args.out / 'compiler-notes.json'))
            self.assertEqual([args.out / 'counters' / (hit + '.gcda')],
                             [path for path in (args.out / 'counters').rglob('*') if path.is_file()])
            self.assertEqual(counter_file([2, 3]),
                             (args.out / 'counters' / (hit + '.gcda')).read_bytes())
            self.assertEqual({hit + '.gcda': {'counters': 2, 'total': 5}},
                             matrix.read(args.out / 'counters.json'))
            with self.assertRaisesRegex(ValueError, 'already exists'):
                matrix.collect(args)

            problems = {
                'negative or overflowed': lambda: (build / (hit + '.gcda')).write_bytes(
                    counter_file([2, -3])),
                'without a compiler note': lambda: (build / 'CMakeFiles/product.dir/stray.gcda')
                .write_bytes(counter_file([1])),
                'not a regular file': lambda: (build / (cold + '.gcda')).symlink_to(
                    build / (hit + '.gcda')),
                'no coverage counter files': lambda: (build / (hit + '.gcda')).unlink(),
            }
            for message, damage in problems.items():
                with self.subTest(message=message):
                    for path in build.rglob('*.gcda'):
                        path.unlink()
                    (build / (hit + '.gcda')).write_bytes(counter_file([2, 3]))
                    damage()
                    args.out = root / f'results-{message.split()[0]}'
                    with self.assertRaisesRegex(ValueError, message):
                        matrix.collect(args)

    @unittest.skipUnless(shutil.which('zstd'), 'the CI shells provide zstd')
    def test_note_archive_restores_exactly_the_planned_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / 'build'
            names = ['CMakeFiles/a.dir/src/a.cpp.gcno', 'tests/CMakeFiles/t.dir/t.cpp.gcno']
            for name in names:
                (build / name).parent.mkdir(parents=True)
                (build / name).write_bytes(b'note ' + name.encode() * 50)
            (build / 'CMakeFiles/a.dir/src/a.cpp.gcda').write_bytes(counter_file([1]))
            (build / 'CMakeFiles/a.dir/src/a.cpp.o').write_bytes(b'object')
            archive = root / 'shipped' / matrix.NOTES_ARCHIVE
            matrix.pack_notes(build, archive)
            expected = matrix.compiler_notes(build)
            tree = root / 'gate' / 'build'
            matrix.unpack_notes(archive, tree, expected)
            self.assertEqual(sorted(names), sorted(
                str(path.relative_to(tree)) for path in tree.rglob('*') if path.is_file()))
            self.assertEqual(expected, matrix.compiler_notes(tree))
            with self.assertRaisesRegex(ValueError, 'already exists'):
                matrix.unpack_notes(archive, tree, expected)

            # A note the plan does not list, a note the plan lists and the
            # archive lacks, and a note whose bytes changed are all refused.
            fewer = {names[0]: expected[names[0]]}
            with self.assertRaisesRegex(ValueError, 'unplanned entry'):
                matrix.unpack_notes(archive, root / 'gate-fewer', fewer)
            more = dict(expected, **{'CMakeFiles/a.dir/src/z.cpp.gcno': 'f' * 64})
            with self.assertRaisesRegex(ValueError, "not the planned build's"):
                matrix.unpack_notes(archive, root / 'gate-more', more)
            changed = dict(expected, **{names[0]: '0' * 64})
            with self.assertRaisesRegex(ValueError, "not the planned build's"):
                matrix.unpack_notes(archive, root / 'gate-changed', changed)
            (root / 'no-notes').mkdir()
            with self.assertRaisesRegex(ValueError, 'no compiler notes'):
                matrix.pack_notes(root / 'no-notes', root / 'empty.tar.zst')

    def worker_counters(self, root, per_worker):
        """per_worker: one {relative gcda: counters} per worker."""
        directories = []
        notes = {}
        for index, files in enumerate(per_worker):
            directory = root / f'shard-{index + 1}' / 'counters'
            directory.mkdir(parents=True)
            for name, counters in files.items():
                (directory / name).parent.mkdir(parents=True, exist_ok=True)
                (directory / name).write_bytes(counter_file(counters))
                notes[str(Path(name).with_suffix('.gcno'))] = 'hash'
            directories.append(directory)
        return directories, notes

    def test_merged_counters_are_proved_to_be_the_sum_of_every_worker(self):
        shared = 'CMakeFiles/game.dir/src/a.cpp.gcda'
        first_only = 'CMakeFiles/unit_x.dir/tests/x.cpp.gcda'
        last_only = 'CMakeFiles/unit_y.dir/tests/y.cpp.gcda'
        workers = [{shared: [1, 0, 5], first_only: [7]},
                   {shared: [10, 0, 50]},
                   {shared: [100, 2, 500], last_only: [9, 9]}]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directories, notes = self.worker_counters(root, workers)
            notes['CMakeFiles/game.dir/src/never_run.cpp.gcno'] = 'hash'
            tree = root / 'tree'
            for note in notes:
                (tree / note).parent.mkdir(parents=True, exist_ok=True)
                (tree / note).write_bytes(b'note')
            (tree / Path(last_only).with_suffix('.gcno')).unlink()
            with mock.patch.object(matrix.subprocess, 'run', side_effect=summing_gcov_tool), \
                    self.assertRaisesRegex(ValueError, 'lacks the note of ' + last_only):
                matrix.merge_counters(directories, tree, notes)
            self.assertEqual([], list(tree.rglob('*.gcda')))  # nothing is half-installed
            (tree / Path(last_only).with_suffix('.gcno')).write_bytes(b'note')
            with mock.patch.object(matrix.subprocess, 'run', side_effect=summing_gcov_tool) as tool:
                merged = matrix.merge_counters(directories, tree, notes)
            self.assertEqual(2, tool.call_count)  # one merge per additional worker
            self.assertEqual({shared: (3, 668), first_only: (1, 7), last_only: (2, 18)}, merged)
            self.assertEqual([111, 2, 555], one_function_counters(tree / shared))
            self.assertEqual([7], one_function_counters(tree / first_only))
            self.assertEqual(sorted(merged), sorted(
                str(path.relative_to(tree)) for path in tree.rglob('*.gcda')))
            self.assertEqual([], list(root.glob('counter-merge-*')))
            with mock.patch.object(matrix.subprocess, 'run', side_effect=summing_gcov_tool), \
                    self.assertRaisesRegex(ValueError, 'already holds counters'):
                matrix.merge_counters(directories, tree, notes)

    def test_a_merge_that_is_not_the_sum_is_refused(self):
        shared = 'CMakeFiles/game.dir/src/a.cpp.gcda'
        other = 'CMakeFiles/game.dir/src/b.cpp.gcda'
        workers = [{shared: [1, 5], other: [3]}, {shared: [10, 50]}]

        def keeps_the_first(command, **kwargs):
            target, first = Path(command[3]), Path(command[4])
            shutil.copytree(first, target)
            return subprocess.CompletedProcess(command, 0, stdout='')

        def drops_a_file(command, **kwargs):
            result = summing_gcov_tool(command, **kwargs)
            (Path(command[3]) / other).unlink()
            return result

        def complains(command, **kwargs):
            summing_gcov_tool(command, **kwargs)
            return subprocess.CompletedProcess(command, 0, stdout='profile mismatch\n')

        def fails(command, **kwargs):
            return subprocess.CompletedProcess(command, 1, stdout='')

        for name, tool, message in (
                ('lost updates', keeps_the_first, 'not the sum'),
                ('lost file', drops_a_file, 'not the sum'),
                ('warning', complains, 'could not merge worker counters: profile mismatch'),
                ('failure', fails, 'could not merge worker counters')):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directories, notes = self.worker_counters(root, workers)
                (root / 'tree').mkdir()
                with mock.patch.object(matrix.subprocess, 'run', side_effect=tool), \
                        self.assertRaisesRegex(ValueError, message):
                    matrix.merge_counters(directories, root / 'tree', notes)
                self.assertEqual([], list((root / 'tree').rglob('*.gcda')))
        # Workers that disagree about one object's counter count ran different builds.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directories, notes = self.worker_counters(
                root, [{shared: [1, 5]}, {shared: [1, 5, 9]}])
            (root / 'tree').mkdir()
            with mock.patch.object(matrix.subprocess, 'run') as tool, \
                    self.assertRaisesRegex(ValueError, 'disagree about the counters'):
                matrix.merge_counters(directories, root / 'tree', notes)
            tool.assert_not_called()

    def test_the_one_scan_runs_gcov_once_per_object_directory_beside_its_objects(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tree, out = root / 'build', root / 'results'
            out.mkdir()
            notes = {'CMakeFiles/product.dir/src/a.cpp.gcno': 'hash',
                     'CMakeFiles/product2.dir/src/b.cpp.gcno': 'hash',
                     'CMakeFiles/Testing.dir/src/a.cpp.gcno': 'hash'}
            exclusions = [r'(?:.*/)?CMakeFiles/Testing\.dir(?:/|$)']
            reporter = SimpleNamespace(
                canonicalize_cpp_json=mock.Mock(),
                GCOVR_SUSPICIOUS_HITS_THRESHOLD=(1 << 63) - 1)
            with mock.patch.dict('sys.modules', {'coverage_report': reporter}), \
                 mock.patch.object(matrix, 'compiler_notes', return_value=notes), \
                 mock.patch.object(matrix.subprocess, 'run') as execute:
                matrix.scan_counters(tree, exclusions, out, 4)
            commands = [call.args[0] for call in execute.call_args_list]
            scanned = []
            traces = {}
            for command in commands:
                self.assertTrue(all(call.kwargs == {'check': True} for call in execute.call_args_list))
                roots = [command[i+1] for i, value in enumerate(command) if value == '--filter']
                self.assertEqual(roots, ['src/', 'include/openglad/'])
                self.assertFalse(any(flag.startswith('--gcov-ignore-parse-errors') for flag in command))
                self.assertNotIn('--gcov-exclude-directory', command)
                self.assertEqual(command[command.index('--gcov-suspicious-hits-threshold')+1],
                                 str((1 << 63) - 1))
                # gcov runs beside each object: a shared working directory
                # would serialize every scan behind one lock.
                self.assertEqual(command[command.index('--gcov-object-directory')+1], '.')
                self.assertEqual(command[command.index('-j')+1], '1')
                trace = command[command.index('--json')+1]
                directories = command[command.index('--json')+2:]
                self.assertNotIn(str(tree), directories)  # never the whole tree twice
                traces[trace] = directories
                scanned.extend(directories)
            self.assertEqual(sorted(scanned), sorted(
                str(tree / name) for name in
                ('CMakeFiles/product.dir', 'CMakeFiles/product2.dir', 'CMakeFiles/Testing.dir')))
            merges = {call.args[0]: call for call in reporter.canonicalize_cpp_json.call_args_list}
            self.assertEqual(set(merges), {out / 'cpp.json', out / 'production.json'})
            for call in merges.values():
                self.assertEqual(call.args[1], Path.cwd())
            everything = [str(path) for path in merges[out / 'cpp.json'].kwargs['sources']]
            production = [str(path) for path in merges[out / 'production.json'].kwargs['sources']]
            self.assertEqual(sorted(everything), sorted(traces))
            self.assertTrue(set(production) < set(everything))
            self.assertEqual(sorted(d for trace in production for d in traces[trace]), sorted(
                str(tree / name) for name in ('CMakeFiles/product.dir', 'CMakeFiles/product2.dir')))
            self.assertEqual(list(out.glob('cpp-partitions-*')), [])

    def gate_inputs(self, root):
        """A planned two-worker run with complete, well-formed evidence."""
        plan = self.plan()
        note = 'CMakeFiles/game.dir/src/a.cpp.gcno'
        plan['compiler_notes'] = {note: 'hash'}
        plan['source_root'] = str(Path.cwd().resolve())
        plan['build_relative'] = 'build/ci-coverage'
        plan['production_exclusions'] = [r'(?:.*/)?CMakeFiles/Testing\.dir(?:/|$)']
        plan['lines_tool_sha256'] = matrix.hashlib.sha256(b'oracle').hexdigest()
        plan_file = root / 'plan.json'
        matrix.write(plan_file, plan)
        results = root / 'results'
        for report in self.reports(plan):
            directory = results / f'shard-{report["shard"]}'
            matrix.write(directory / 'result.json', report)
            matrix.write(directory / 'compiler-notes.json', plan['compiler_notes'])
            counters = directory / 'counters' / Path(note).with_suffix('.gcda')
            counters.parent.mkdir(parents=True)
            counters.write_bytes(counter_file([report['shard']]))
            (directory / 'lua-raw').mkdir()
            (directory / 'lua-raw/dump.luacov').write_text('process evidence')
        tool = root / 'plan/og_lua_lines'
        tool.parent.mkdir()
        tool.write_bytes(b'oracle')
        tool.chmod(0o644)
        return plan, plan_file, results, tool

    def run_gate(self, args, reporter=None, **replaced):
        """Run merge() with the compiler-facing stages replaced by recorders."""
        reporter = reporter or SimpleNamespace(
            gcovr_lcov_1x_flag=mock.Mock(return_value='--lcov-format-version=1.x'))
        stages = {name: mock.Mock(name=name)
                  for name in ('unpack_notes', 'merge_counters', 'compiler_notes')}
        stages.update(replaced)

        def scanned(tree, exclusions, out, workers=4):
            for name in ('cpp.json', 'production.json'):
                matrix.write(out / name, {'files': []})

        stages.setdefault('scan_counters', mock.Mock(side_effect=scanned))
        with mock.patch.dict('sys.modules', {'coverage_report': reporter}), \
             mock.patch.multiple(matrix, **stages), \
             mock.patch.object(matrix.subprocess, 'run') as execute:
            matrix.merge(args)
        return reporter, stages, execute

    def test_gate_sums_the_workers_scans_once_and_formats_both_object_populations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan, plan_file, results, tool = self.gate_inputs(root)
            notes_archive = root / 'notes' / matrix.NOTES_ARCHIVE
            args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                   build=None, lines_tool=tool, notes=notes_archive)
            reporter, stages, execute = self.run_gate(args)
            tree = Path('build/ci-coverage')
            stages['unpack_notes'].assert_called_once_with(
                notes_archive, tree, plan['compiler_notes'])
            stages['merge_counters'].assert_called_once_with(
                [results / 'shard-1/counters', results / 'shard-2/counters'],
                tree, plan['compiler_notes'])
            stages['scan_counters'].assert_called_once_with(
                tree, plan['production_exclusions'], args.out)
            reporter.gcovr_lcov_1x_flag.assert_called_once_with(['gcovr'], Path.cwd())
            self.assertEqual(execute.call_count, 4)
            commands = [call.args[0] for call in execute.call_args_list]
            self.assertTrue(all(call.kwargs == {'check': True} for call in execute.call_args_list))

            def only(marker):
                matches = [command for command in commands
                           if command[0] == 'gcovr' and marker in command]
                self.assertEqual(len(matches), 1, marker)
                return matches[0]

            primary = only(str(args.out / 'cpp.info'))
            production = only(str(args.out / 'production.info'))
            detail = only('--html-details')
            gate = commands[-1]  # The gate starts only after both tracefiles exist.
            self.assertIn('--cpp-production-tracefile', gate)
            for command, filename in ((primary, 'cpp.info'), (production, 'production.info')):
                self.assertIn('--lcov-format-version=1.x', command)
                self.assertNotIn('--lcov-format-1.x', command)
                self.assertIn(str(args.out / filename), command)
                self.assertNotIn(str(tree), command)  # formatting never scans profiles
                self.assertNotIn('--html-details', command)
                # Each population is formatted from its one scanned trace.
                self.assertEqual(1, command.count('--add-tracefile'))
            self.assertEqual(primary[primary.index('--add-tracefile')+1],
                             detail[detail.index('--add-tracefile')+1])
            self.assertNotEqual(primary[primary.index('--add-tracefile')+1],
                                production[production.index('--add-tracefile')+1])
            self.assertEqual(gate[gate.index('--lines-tool')+1], str(tool))
            self.assertTrue(tool.stat().st_mode & 0o100)  # artifacts drop the executable bit
            self.assertEqual(sorted(path.name for path in (args.out / 'lua-raw').iterdir()),
                             ['shard-1-dump.luacov', 'shard-2-dump.luacov'])
            self.assertFalse((args.out / 'scanned').exists())

    def test_gate_over_a_restored_build_uses_its_notes_in_place(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan, plan_file, results, _ = self.gate_inputs(root)
            build = root / 'restored'
            args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                   build=build, lines_tool=None, notes=None)
            same = mock.Mock(return_value=plan['compiler_notes'])
            reporter, stages, execute = self.run_gate(args, compiler_notes=same)
            stages['unpack_notes'].assert_not_called()
            same.assert_called_once_with(build)
            self.assertEqual(build, stages['merge_counters'].call_args.args[1])
            gate = execute.call_args_list[-1].args[0]
            self.assertEqual(gate[gate.index('--lines-tool')+1], str(build / 'og_lua_lines'))
            args.out = root / 'output-other'
            other = mock.Mock(return_value={'another.gcno': 'hash'})
            with self.assertRaisesRegex(ValueError, 'compiler notes differ'):
                self.run_gate(args, compiler_notes=other)

    def test_gate_refuses_incomplete_or_foreign_evidence_before_scanning(self):
        problems = {
            'missing C\\+\\+ counter files': lambda results, tool: shutil.rmtree(
                results / 'shard-2/counters'),
            'compiler notes differ': lambda results, tool: matrix.write(
                results / 'shard-2/compiler-notes.json', {'another.gcno': 'hash'}),
            'missing Lua dumps': lambda results, tool: (
                results / 'shard-1/lua-raw/dump.luacov').unlink(),
            'executable-line oracle is not': lambda results, tool: tool.write_bytes(b'another build'),
            'executable-line oracle missing': lambda results, tool: tool.unlink(),
        }
        for message, damage in problems.items():
            with self.subTest(message=message), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                plan, plan_file, results, tool = self.gate_inputs(root)
                damage(results, tool)
                args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                       build=None, lines_tool=tool,
                                       notes=root / matrix.NOTES_ARCHIVE)
                with self.assertRaisesRegex(ValueError, message):
                    self.run_gate(args)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan, plan_file, results, tool = self.gate_inputs(root)
            del plan['lines_tool_sha256']
            plan['build_relative'] = '../elsewhere'
            matrix.write(plan_file, plan)
            for report in self.reports(plan):
                matrix.write(results / f'shard-{report["shard"]}/result.json', report)
            args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                   build=None, lines_tool=tool,
                                   notes=root / matrix.NOTES_ARCHIVE)
            with self.assertRaisesRegex(ValueError, 'not inside the checkout'):
                self.run_gate(args)

    def test_gate_makes_and_removes_the_producers_source_alias(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = Path.cwd().resolve()
            self.assertIsNone(matrix.source_alias(str(repo), repo))
            canonical = root / 'producer' / 'source'
            alias = matrix.source_alias(str(canonical), repo)
            self.assertEqual(canonical, alias)
            self.assertEqual(repo, canonical.resolve())
            # A second gate on this machine finds the same alias and leaves it.
            self.assertIsNone(matrix.source_alias(str(canonical), repo))
            alias.unlink()
            canonical.mkdir()
            with self.assertRaisesRegex(ValueError, 'in use by another checkout'):
                matrix.source_alias(str(canonical), repo)

            plan, plan_file, results, tool = self.gate_inputs(root)
            elsewhere = root / 'producer' / 'alias'
            plan['source_root'] = str(elsewhere)
            matrix.write(plan_file, plan)
            for report in self.reports(plan):
                matrix.write(results / f'shard-{report["shard"]}/result.json', report)
            seen = []
            args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                   build=None, lines_tool=tool,
                                   notes=root / matrix.NOTES_ARCHIVE)
            self.run_gate(args, merge_counters=mock.Mock(
                side_effect=lambda *a: seen.append(elsewhere.resolve())))
            self.assertEqual([repo], seen)          # present while gcov runs
            self.assertFalse(elsewhere.is_symlink())  # gone afterwards
            failing = mock.Mock(side_effect=ValueError('scan failed'))
            args.out = root / 'output-failed'
            with self.assertRaisesRegex(ValueError, 'scan failed'):
                self.run_gate(args, scan_counters=failing)
            self.assertFalse(elsewhere.is_symlink())  # and after a failure too

    def test_optional_dependency_cannot_silently_drop_curses_or_parity(self):
        required = ['og_unit_network', 'og_unit_network_ws_server',
                    'og_unit_network_ws_client', 'og_unit_core', 'og_test_curses',
                    'openglad_curses_link_no_sdl', 'openglad_server_link_no_sdl',
                    'semantics_check', 'og_test_parity', 'og_test_parity_a_m',
                    'og_test_parity_gates', 'scenario99_setup', 'scenario99_cleanup',
                    'parity_census_setup', 'parity_census_cleanup',
                    *[f'og_test_parity_census_{part}' for part in range(4)]]
        inventory = {'tests': [{'name': name} for name in required]}
        matrix.validate_product_inventory(inventory, 'coverage')
        for absent in required:
            with self.subTest(absent=absent), self.assertRaisesRegex(ValueError, absent):
                matrix.validate_product_inventory(
                    {'tests': [test for test in inventory['tests'] if test['name'] != absent]},
                    'coverage')

    def test_disabled_inventory_and_absent_network_rejected(self):
        inventory = self.inventory()
        inventory['tests'][3]['properties'] = [{'name': 'DISABLED', 'value': True}]
        with self.assertRaisesRegex(ValueError, 'disabled'):
            matrix.partition(inventory, {}, 2, 'coverage')
        with self.assertRaisesRegex(ValueError, 'network'):
            matrix.partition(self.inventory(), {}, 1, 'tsan')

    def test_focused_tsan_lane_runs_every_network_entry_and_nothing_else(self):
        entries = sorted(matrix.NETWORK_ENTRIES)
        inventory = {'tests': [{'name': name} for name in
                               [*entries, 'og_unit_core', 'og_test_picker_network']]}
        plan = matrix.partition(inventory, {}, 1, 'tsan')
        self.assertEqual(entries, plan['shards'][0]['tests'])
        self.assertEqual(['og_test_picker_network', 'og_unit_core'],
                         plan['focused_lane_omissions'])
        matrix.validate_product_inventory(inventory, 'tsan')
        for absent in entries:
            with self.subTest(absent=absent):
                partial = {'tests': [test for test in inventory['tests']
                                     if test['name'] != absent]}
                with self.assertRaisesRegex(ValueError, absent):
                    matrix.partition(partial, {}, 1, 'tsan')
                with self.assertRaisesRegex(ValueError, absent):
                    matrix.validate_product_inventory(partial, 'tsan')
        # A fourth cut of the binary must be listed before any lane runs.
        grown = {'tests': inventory['tests'] + [{'name': 'og_unit_network_relay'}]}
        with self.assertRaisesRegex(ValueError, 'og_unit_network_relay'):
            matrix.validate_product_inventory(grown, 'tsan')
        with self.assertRaisesRegex(ValueError, 'og_unit_network_relay'):
            matrix.validate_product_inventory(grown, 'coverage')


if __name__ == '__main__':
    unittest.main()
