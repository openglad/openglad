import copy
import unittest
import browser_matrix as matrix


def report(names, actual=False):
    specs = []
    for line, name in enumerate(names, 1):
        test = {'projectName': 'chromium', 'status': 'expected', 'results': []}
        if actual: test['results'] = [{'status': 'passed'}]
        specs.append({'title': name, 'file': 'example.spec.js', 'line': line,
                      'tests': [test]})
    return {'suites': [{'title': 'example', 'specs': specs}], 'errors': []}


class BrowserMatrixTests(unittest.TestCase):
    def reports(self):
        inventory = report(['one', 'two'])
        # Preserve source locations in selected and actual reports.
        pairs = {}
        for index in (1, 2):
            selected = copy.deepcopy(inventory)
            selected['suites'][0]['specs'] = [selected['suites'][0]['specs'][index-1]]
            actual = copy.deepcopy(selected)
            actual['suites'][0]['specs'][0]['tests'][0]['results'] = [{'status': 'passed'}]
            pairs[index] = (inventory, selected, actual, {'exit_code': 0, 'shard': f'{index}/2'})
        return pairs

    def test_complete_result_union(self):
        self.assertEqual(matrix.verify(self.reports(), 2), {'tests': 2, 'shards': 2, 'scheduled_skips': []})

    def test_same_source_line_and_test_title_in_distinct_suites_is_not_duplicate(self):
        one = report(['same'])['suites'][0]
        two = copy.deepcopy(one)
        two['title'] = 'different parameters'
        self.assertEqual(len(matrix.cases({'suites': [one, two]})), 2)

    def test_first_failure_retry_skip_missing_and_overlap_fail(self):
        for kind in ('failure', 'retry', 'skip', 'missing', 'overlap', 'exit', 'global'):
            reports = copy.deepcopy(self.reports())
            inventory, selected, actual, status = reports[1]
            test = actual['suites'][0]['specs'][0]['tests'][0]
            if kind == 'failure': test['results'][0]['status'] = 'failed'
            if kind == 'retry': test['results'].append({'status': 'passed'})
            if kind == 'skip': test['results'][0]['status'] = 'skipped'
            if kind == 'missing': del reports[2]
            if kind == 'overlap': reports[2] = (inventory, selected, actual, {'exit_code': 0, 'shard': '2/2'})
            if kind == 'exit': status['exit_code'] = 1
            if kind == 'global': actual['errors'] = [{'message': 'server failed'}]
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                matrix.verify(reports, 2)

    def test_null_skip_reason_cannot_match_an_unknown_identity(self):
        reports = self.reports()
        test = reports[1][2]['suites'][0]['specs'][0]['tests'][0]
        test['results'][0]['status'] = 'skipped'
        test['annotations'] = [{'type': 'skip', 'description': None}]
        with self.assertRaisesRegex(ValueError, 'unapproved browser skip'):
            matrix.verify(reports, 2)

    def test_existing_opt_in_skip_is_reported_with_reason(self):
        reports = self.reports()
        prepared = set()
        for item in reports.values():
            for document in item[:3]:
                if id(document) in prepared:
                    continue
                prepared.add(id(document))
                suite = document['suites'][0]
                for spec in suite['specs']:
                    spec['file'] = 'wasm-networking.spec.js'
                    if spec['title'] == 'one':
                        spec['title'] = 'HOST with no override creates a real room on the deployed worker'
                document['suites'] = [{'title': 'wasm-networking.spec.js', 'suites': [suite]}]
                suite['title'] = 'Browser networking live deployed relay (opt-in)'
        test = reports[1][2]['suites'][0]['suites'][0]['specs'][0]['tests'][0]
        reason = 'live-relay test is opt-in: set OPENGLAD_E2E_LIVE_RELAY=1'
        test['results'][0]['status'] = 'skipped'
        test['annotations'] = [{'type': 'skip', 'description': reason}]
        skips = matrix.verify(reports, 2)['scheduled_skips']
        self.assertEqual(len(skips), 1)
        self.assertEqual(skips[0]['reason'], reason)
        # A sibling behavior case cannot borrow an opt-in reason from its file.
        for change in ('title', 'suite', 'project'):
            altered = copy.deepcopy(reports)
            for document in altered[1][:3]:
                suite = document['suites'][0]['suites'][0]
                spec = suite['specs'][0]
                if change == 'title': spec['title'] = 'required new networking behavior'
                if change == 'suite': suite['title'] = 'new required networking suite'
                if change == 'project': spec['tests'][0]['projectName'] = 'new-required-browser'
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, 'unapproved browser skip'):
                matrix.verify(altered, 2)


if __name__ == '__main__':
    unittest.main()
