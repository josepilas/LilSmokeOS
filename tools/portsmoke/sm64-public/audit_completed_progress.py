#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reconcile the real completed build log against its exact dry-run plan."""
import argparse
import json
from pathlib import Path

from build_progress import BuildPlan, BuildProgress, classify_command


def reconcile(plan_path, build_path):
    plan = BuildPlan()
    with plan_path.open() as source:
        for line in source:
            plan.feed(line)
    plan.finish()
    live = BuildProgress(plan)
    with build_path.open() as source:
        for line in source:
            live.feed(line)
    raw = live.finish()
    if raw['plan_matches']:
        return raw
    # GNU make remakes included makefiles even with -n. This exact CPP rule
    # is executed during planning, so it is not pending in the subsequent run.
    # Do not forgive arbitrary missing tasks or manufacture live completions.
    def include_rule(line):
        task = classify_command(line.rstrip('\n'))
        return (task is not None and task.kind == 'generate' and task.tool == 'cc'
                and task.target == 'build/original-mips/level_rules.mk'
                and line.rstrip().endswith(' levels/level_rules.mk'))
    with plan_path.open() as source:
        include_count = sum(include_rule(line) for line in source)
    with build_path.open() as source:
        actual_include_count = sum(include_rule(line) for line in source)
    if include_count != 1 or actual_include_count != 0:
        raise ValueError('Unexplained build-plan mismatch')
    pending = BuildPlan()
    with plan_path.open() as source:
        for line in source:
            if not include_rule(line):
                pending.feed(line)
    pending.finish()
    live = BuildProgress(pending)
    with build_path.open() as source:
        for line in source:
            live.feed(line)
    value = live.finish()
    value['displayed_plan_reconciliation'] = raw
    value['included_makefile_tasks_executed_during_planning'] = 1
    value['live_counter_is_conservative_by_one'] = True
    return value


def audit(root):
    report = json.loads((root / 'report.json').read_text())
    if report.get('passed') is not True or not report.get('full_native_mips_build'):
        raise ValueError('A successful full native build is required')
    cache = root / 'Test App/.sm64-cache'
    current = json.loads((cache / 'current.json').read_text())
    name = current['generation']
    if not name.startswith('ready-') or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in name):
        raise ValueError('Invalid private cache generation')
    folder = cache / name
    value = reconcile(folder / 'plan.log', folder / 'build.log')
    (root / 'progress-audit-diagnostic.json').write_text(json.dumps(value, indent=2) + '\n')
    if not value['plan_matches'] or value['percent'] != 100 or value['completed'] != value['planned']:
        raise ValueError('Real build tasks do not reconcile with the displayed plan')
    if value['by_kind']['texture']['completed'] < 100 or value['by_kind']['compile']['completed'] < 10:
        raise ValueError('The real texture/compiler workload was not exercised')
    value['passed'] = True
    (root / 'progress-audit.json').write_text(json.dumps(value, indent=2) + '\n')
    return value


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    print(json.dumps(audit(parser.parse_args().root), indent=2))
