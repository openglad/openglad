#!/usr/bin/env python3
"""Run browser shards once and account for every listed project/test pair."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import subprocess
import sys

# Existing opt-in captures/live-service probes are visible scheduled skips.
# A new skipped behavior test needs an explicit reviewed entry here.
SKIPS = {
    ('chromium', 'zz-capture-ability-select.spec.js', ('touch overlay still (' + device + ')', title)):
        'set OG_WEB_SHOT_DIR to write the PR stills'
    for device, title in (('iPhone 15 landscape', 'touch-iphone-landscape'),
                          ('iPhone 15 landscape', 'touch-iphone-alt'),
                          ('iPad Pro 11 landscape', 'touch-ipad-landscape'))
} | {
    ('chromium', 'zz-capture-ability-select.spec.js', ('CHANGELOG! page still (web)', 'changelog-web')):
        'set OG_WEB_SHOT_DIR to write the PR stills',
    ('chromium', 'zz-capture-controls.spec.js', ('KEYMAP still (web)', 'keymap-web')):
        'set OG_WEB_SHOT_DIR to write the PR stills',
    ('chromium', 'wasm-networking.spec.js', ('Browser networking live deployed relay (opt-in)',
         'HOST with no override creates a real room on the deployed worker')):
        'live-relay test is opt-in: set OPENGLAD_E2E_LIVE_RELAY=1',
} | {
    (project, 'wasm-touch.spec.js', ('Touch gameplay controls',
         'audio context unlocks from the first touch gesture')):
        'Module.SDL3.audioContext is not exposed'
    for project in ('iphone-touch', 'iphone-touch-portrait')
}


def cases(report):
    found = {}

    def visit(suite, parents=()):
        titles = (*parents, suite["title"])
        for spec in suite.get('specs', []):
            for test in spec['tests']:
                identity = json.dumps([test['projectName'], spec['file'], spec['line'],
                                       spec.get('column', 0), *titles, spec['title']])
                if identity in found:
                    raise ValueError(f'duplicate browser test: {identity}')
                found[identity] = (spec, test)
        for child in suite.get('suites', []):
            visit(child, titles)
    for suite in report['suites']:
        visit(suite)
    return found


def run(args):
    args.out.mkdir(parents=True, exist_ok=True)
    projects = [f'--project={p}' for p in args.project]
    base = ['npx', 'playwright', 'test', '--workers=1', '--retries=0', *projects]
    paths = {key: (args.out / f'{key}.json').resolve() for key in ('inventory', 'selected', 'actual')}
    for key, extra in [('inventory', ['--list']), ('selected', ['--list', f'--shard={args.shard}'])]:
        env = dict(os.environ, PLAYWRIGHT_JSON_OUTPUT_NAME=str(paths[key]))
        subprocess.run([*base, *extra, '--reporter=json'], env=env, check=True)
    env = dict(os.environ, PLAYWRIGHT_JSON_OUTPUT_NAME=str(paths['actual']))
    process = subprocess.run([*base, f'--shard={args.shard}', '--reporter=line,json'], env=env,
                             stdin=subprocess.DEVNULL, check=False)
    (args.out / 'status.json').write_text(json.dumps({'exit_code': process.returncode, 'shard': args.shard})+'\n')
    if process.returncode:
        raise ValueError(f'browser shard failed: exit {process.returncode}')


def verify(reports, count):
    if Counter(reports.keys()) != Counter({i: 1 for i in range(1, count+1)}):
        raise ValueError('missing or unexpected browser shard')
    complete = None
    union = []
    skipped = []
    for index, (inventory, selected, actual, status) in reports.items():
        full = set(cases(inventory))
        if complete is None: complete = full
        if full != complete: raise ValueError('browser inventory changed between shards')
        if status != {'exit_code': 0, 'shard': f'{index}/{count}'}:
            raise ValueError(f'browser shard {index} failed or used a different partition')
        planned, results = cases(selected), cases(actual)
        if set(planned) != set(results):
            raise ValueError(f'browser shard {index} has missing/extra results')
        union.extend(results)
        if actual.get('errors'):
            raise ValueError(f'browser shard {index} reported global errors')
        for identity, (spec, test) in results.items():
            attempts = test.get('results', [])
            if len(attempts) != 1:
                raise ValueError(f'browser test was retried or never ran: {identity}')
            result = attempts[0]
            if result['status'] == 'skipped':
                annotations = test.get('annotations', []) + result.get('annotations', [])
                reason = next((a.get('description', '') for a in annotations if a['type']=='skip'), '')
                # The identity includes the project and enclosing suites. A
                # same-named case elsewhere cannot borrow this opt-in waiver.
                labels = json.loads(identity)
                skip_key = (test['projectName'], Path(spec['file']).name, tuple(labels[5:]))
                if skip_key not in SKIPS or SKIPS[skip_key] != reason:
                    raise ValueError(f'unapproved browser skip: {identity}: {reason}')
                skipped.append({'test': identity, 'reason': reason})
            elif result['status'] != 'passed' or test.get('status') != 'expected':
                raise ValueError(f'browser test failed: {identity}')
    if len(union) != len(set(union)) or set(union) != complete or not union:
        raise ValueError('browser shard union is incomplete or overlapping')
    return {'tests': len(union), 'shards': count, 'scheduled_skips': skipped}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('run')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--shard', required=True)
    p.add_argument('--project', action='append', default=[])
    p = sub.add_parser('verify')
    p.add_argument('--results', type=Path, required=True)
    p.add_argument('--shards', type=int, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'run': run(args)
        else:
            reports = {}
            for directory in args.results.glob('shard-*'):
                index = int(directory.name.removeprefix('shard-'))
                reports[index] = tuple(json.loads((directory / f'{name}.json').read_text())
                                       for name in ('inventory', 'selected', 'actual', 'status'))
            result = verify(reports, args.shards)
            print(json.dumps(result, indent=2))
            if os.getenv('GITHUB_STEP_SUMMARY'):
                with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as out:
                    out.write('Browser results and explicit scheduled skips:\n```json\n'+json.dumps(result, indent=2)+'\n```\n')
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'browser_matrix: {error}\n')


if __name__ == '__main__':
    main()
