#!/usr/bin/env python3
"""Require every selected parity mutation row and group across all workers."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys


def verify(plan, reports):
    shards = {s['id']: s for s in plan['shards']}
    if Counter(i for i, _ in reports) != Counter({i: 1 for i in shards}):
        raise ValueError('missing, unexpected or duplicate canary shard')
    expected_groups = [tuple(group) for shard in shards.values() for group in shard['groups']]
    if len(expected_groups) != len(set(expected_groups)):
        raise ValueError('mutation group assigned to multiple workers')
    selected = [name for shard in shards.values() for name in shard['rows']]
    if len(selected) != len(set(selected)) or set(selected) != set(plan['rows']):
        raise ValueError('canary manifest does not cover full selected corpus')
    for index, report in reports:
        names = [row['id'] for row in report['rows']]
        if Counter(names) != Counter(shards[index]['rows']):
            raise ValueError(f'shard {index}: missing, repeated or unexpected row result')
        if report['tallies']['rows'] != len(names):
            raise ValueError(f'shard {index}: inconsistent row count')
        for row in report['rows']:
            if row['pred_flips'] < 1:
                raise ValueError(f'shard {index}: predicate-toothless row {row["id"]}')
        if report['tallies']['zero_flips'] or report['tallies']['predicate_toothless']:
            raise ValueError(f'shard {index}: mutation failures')
    return {'rows': len(selected), 'groups': len(expected_groups), 'shards': len(shards)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    args = parser.parse_args()
    try:
        reports = [(int(p.parent.name.removeprefix('shard-')), json.loads(p.read_text()))
                   for p in args.results.glob('shard-*/report.json')]
        result = verify(json.loads(args.plan.read_text()), reports)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'canary_matrix: {error}\n')


if __name__ == '__main__':
    main()
