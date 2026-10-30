#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Stage a complete file-backed M8 runtime without modifying a card or image."""
import argparse
import json
from pathlib import Path
import shutil
from build_stock_candidate import prepare_runtime, prepare_boot_logo, build_display_adapter, BOARD, sha

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
result = prepare_runtime(args.build / 'target', args.out)
build_display_adapter(args.build, args.out / 'libm8-display.so')
result['boot_logo'] = prepare_boot_logo(args.build, args.out)
shutil.copyfile(BOARD / 'm8-launch.sh', args.out / 'm8-launch.sh')
result['launcher_sha256'] = sha(args.out / 'm8-launch.sh')
result['physical_card_modified'] = False
(args.out / 'staging-report.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))

