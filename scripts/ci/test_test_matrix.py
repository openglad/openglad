#!/usr/bin/env python3
"""The scheduler must lose neither tests nor failed/missing evidence."""
import copy
import tempfile
from pathlib import Path
import unittest
from unittest import mock
from types import SimpleNamespace

import test_matrix as matrix


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

    def test_zero_hit_trace_grid_retains_functions_and_ignores_counts(self):
        trace = {'files': [{'file': 'src/example.cpp',
                           'lines': [{'line_number': 8, 'count': 0}],
                           'functions': [{'name': 'cold', 'lineno': 8, 'execution_count': 0}]}]}
        hit = copy.deepcopy(trace)
        hit['files'][0]['lines'][0]['count'] = 10
        hit['files'][0]['functions'][0]['execution_count'] = 10
        self.assertEqual(matrix.trace_grid(trace), matrix.trace_grid(hit))
        del hit['files'][0]['functions'][0]
        self.assertNotEqual(matrix.trace_grid(trace), matrix.trace_grid(hit))
        with self.assertRaisesRegex(ValueError, 'empty'):
            matrix.trace_grid({'files': []})

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

    def test_collection_scans_each_object_directory_once_beside_its_objects(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = SimpleNamespace(build=root / 'build', out=root / 'results', parallel=4)
            args.out.mkdir()
            notes = {'CMakeFiles/product.dir/src/a.cpp.gcno': 'hash',
                     'CMakeFiles/product2.dir/src/b.cpp.gcno': 'hash',
                     'CMakeFiles/Testing.dir/src/a.cpp.gcno': 'hash'}
            reporter = SimpleNamespace(
                production_variant_exclusions=lambda build: [r'(?:.*/)?CMakeFiles/Testing\.dir(?:/|$)'],
                canonicalize_cpp_json=mock.Mock(),
                GCOVR_SUSPICIOUS_HITS_THRESHOLD=(1 << 63) - 1)
            with mock.patch.dict('sys.modules', {'coverage_report': reporter}), \
                 mock.patch.object(matrix, 'compiler_notes', return_value=notes), \
                 mock.patch.object(matrix.subprocess, 'run') as execute:
                matrix.collect(args)
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
                self.assertNotIn(str(args.build), directories)  # never the whole tree twice
                traces[trace] = directories
                scanned.extend(directories)
            self.assertEqual(sorted(scanned), sorted(
                str(args.build / name) for name in
                ('CMakeFiles/product.dir', 'CMakeFiles/product2.dir', 'CMakeFiles/Testing.dir')))
            merges = {call.args[0]: call for call in reporter.canonicalize_cpp_json.call_args_list}
            self.assertEqual(set(merges), {args.out / 'cpp.json', args.out / 'production.json'})
            for call in merges.values():
                self.assertEqual(call.args[1], Path.cwd())
            everything = [str(path) for path in merges[args.out / 'cpp.json'].kwargs['sources']]
            production = [str(path) for path in merges[args.out / 'production.json'].kwargs['sources']]
            self.assertEqual(sorted(everything), sorted(traces))
            self.assertTrue(set(production) < set(everything))
            self.assertEqual(sorted(d for trace in production for d in traces[trace]), sorted(
                str(args.build / name) for name in ('CMakeFiles/product.dir', 'CMakeFiles/product2.dir')))
            self.assertEqual(matrix.read(args.out / 'compiler-notes.json'), notes)
            self.assertEqual(list(args.out.glob('cpp-partitions-*')), [])

    def test_missing_or_changed_production_inventory_fails_before_reporting(self):
        for problem in ('missing', 'changed'):
            with self.subTest(problem=problem), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                plan = self.plan()
                plan['compiler_notes'] = {'note.gcno': 'hash'}
                plan_file = root / 'plan.json'
                matrix.write(plan_file, plan)
                results = root / 'results'
                for report in self.reports(plan):
                    directory = results / f'shard-{report["shard"]}'
                    matrix.write(directory / 'result.json', report)
                    matrix.write(directory / 'compiler-notes.json', plan['compiler_notes'])
                    trace = {'files': [{'file': 'src/example.cpp',
                                        'lines': [{'line_number': 1, 'count': 0}], 'functions': []}]}
                    matrix.write(directory / 'cpp.json', trace)
                    if not (problem == 'missing' and report['shard'] == 2):
                        production = copy.deepcopy(trace)
                        if problem == 'changed' and report['shard'] == 2:
                            production['files'][0]['lines'][0]['line_number'] = 2
                        matrix.write(directory / 'production.json', production)
                    (directory / 'lua-raw').mkdir()
                    (directory / 'lua-raw/dump.luacov').write_text('process evidence')
                args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                       build=root / 'build')
                with mock.patch.object(matrix.subprocess, 'run') as execute, \
                     self.assertRaisesRegex(ValueError, 'production'):
                    matrix.merge(args)
                execute.assert_not_called()

    def test_merge_formats_both_object_populations_with_the_advertised_lcov_flag(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = self.plan()
            plan['compiler_notes'] = {'note.gcno': 'hash'}
            plan_file = root / 'plan.json'
            matrix.write(plan_file, plan)
            results = root / 'results'
            trace = {'files': [{'file': 'src/example.cpp',
                                'lines': [{'line_number': 1, 'count': 0}], 'functions': []}]}
            for report in self.reports(plan):
                directory = results / f'shard-{report["shard"]}'
                matrix.write(directory / 'result.json', report)
                matrix.write(directory / 'compiler-notes.json', plan['compiler_notes'])
                for filename in ('cpp.json', 'production.json'):
                    matrix.write(directory / filename, trace)
                (directory / 'lua-raw').mkdir()
                (directory / 'lua-raw/dump.luacov').write_text('process evidence')
            args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                   build=root / 'build')
            reporter = SimpleNamespace(gcovr_lcov_1x_flag=mock.Mock(
                return_value='--lcov-format-version=1.x'))
            with mock.patch.dict('sys.modules', {'coverage_report': reporter}), \
                 mock.patch.object(matrix.subprocess, 'run') as execute:
                matrix.merge(args)
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
                self.assertNotIn(str(args.build), command)  # JSON formatting never scans profiles.
                self.assertNotIn('--html-details', command)
            # The HTML detail and the gate tracefile format the same saved traces.
            self.assertEqual([value for value in detail if value.endswith('cpp.json')],
                             [value for value in primary
                              if value.endswith('cpp.json') and value != str(args.out / 'cpp.json')])
            self.assertEqual(gate[gate.index('--lines-tool')+1], str(args.build / 'og_lua_lines'))

    def test_shipped_line_oracle_must_be_the_planned_builds_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tool = root / 'plan/og_lua_lines'
            tool.parent.mkdir()
            tool.write_bytes(b'oracle')
            tool.chmod(0o644)
            plan = self.plan()
            plan['compiler_notes'] = {'note.gcno': 'hash'}
            plan['lines_tool_sha256'] = matrix.hashlib.sha256(b'oracle').hexdigest()
            plan_file = root / 'plan.json'
            matrix.write(plan_file, plan)
            results = root / 'results'
            trace = {'files': [{'file': 'src/example.cpp',
                                'lines': [{'line_number': 1, 'count': 0}], 'functions': []}]}
            for report in self.reports(plan):
                directory = results / f'shard-{report["shard"]}'
                matrix.write(directory / 'result.json', report)
                matrix.write(directory / 'compiler-notes.json', plan['compiler_notes'])
                for filename in ('cpp.json', 'production.json'):
                    matrix.write(directory / filename, trace)
                (directory / 'lua-raw').mkdir()
                (directory / 'lua-raw/dump.luacov').write_text('process evidence')
            reporter = SimpleNamespace(gcovr_lcov_1x_flag=mock.Mock(return_value='--lcov-format-1.x'))
            args = SimpleNamespace(plan=plan_file, results=results, out=root / 'output',
                                   build=None, lines_tool=tool)
            with mock.patch.dict('sys.modules', {'coverage_report': reporter}), \
                 mock.patch.object(matrix.subprocess, 'run') as execute:
                matrix.merge(args)
            gate = execute.call_args_list[-1].args[0]
            self.assertEqual(gate[gate.index('--lines-tool')+1], str(tool))
            self.assertTrue(tool.stat().st_mode & 0o100)  # artifacts drop the executable bit

            for problem in ('changed', 'missing', 'unpinned'):
                with self.subTest(problem=problem):
                    args.out = root / f'output-{problem}'
                    if problem == 'changed':
                        tool.write_bytes(b'another build')
                    elif problem == 'missing':
                        tool.unlink()
                    else:
                        tool.write_bytes(b'oracle')
                        del plan['lines_tool_sha256']
                        matrix.write(plan_file, plan)
                        for report in self.reports(plan):
                            matrix.write(results / f'shard-{report["shard"]}/result.json', report)
                    with mock.patch.dict('sys.modules', {'coverage_report': reporter}), \
                         mock.patch.object(matrix.subprocess, 'run') as execute, \
                         self.assertRaisesRegex(ValueError, 'executable-line oracle'):
                        matrix.merge(args)
                    execute.assert_not_called()

    def test_optional_dependency_cannot_silently_drop_curses_or_parity(self):
        required = ['og_unit_network', 'og_unit_core', 'og_test_curses',
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


if __name__ == '__main__':
    unittest.main()
