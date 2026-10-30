#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Validate public document links/examples without contacting the network."""
import argparse
import json
from pathlib import Path
import re
from urllib.parse import unquote

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--public-logo',type=Path,help='explicit existing public logo when validating the un-staged tree')
args=parser.parse_args()
files=[ROOT/p for p in ('README.md','LICENSE_POLICY.md','THIRD_PARTY_NOTICES.md','CREDITS.md','CONTRIBUTING.md','CHANGELOG.md','docs/release-preparation.md')]
files += list((ROOT/'docs/pre-alpha-0.0.1').glob('*.md'))+list((ROOT/'docs/developer').glob('*.md'))
links=examples=0
for path in files:
    text=path.read_text(encoding='utf-8')
    assert text.count('```')%2==0,path
    for target in re.findall(r'\]\(([^)]+)\)',text):
        target=target.strip('<>')
        if target.startswith(('https://','http://','#','mailto:')): continue
        target=unquote(target.split('#')[0]);actual=(path.parent/target).resolve()
        if not actual.exists() and path==ROOT/'README.md' and target=='logo.webp':
            assert args.public_logo and args.public_logo.is_file(), 'Supply --public-logo or validate the staged tree'
        else:
            assert actual.exists(),(path.relative_to(ROOT),target)
        links+=1
    for block in re.findall(r'^```json[ \t]*\n(.*?)^```[ \t]*$',text,re.S|re.M):
        try: json.loads(block)
        except ValueError as error: raise ValueError(str(path.relative_to(ROOT))+': '+str(error)+'; '+repr(block[:180])) from error
        examples+=1
    assert not re.search(r'(?i)(?:[A-Z]:[\\/]+Users[\\/]+[a-z0-9_ .-]+|/(?:root|home)/[a-z0-9_.-]+/)',text),path
assert 'José Pilas' in (ROOT/'CREDITS.md').read_text(encoding='utf-8')
print(json.dumps(dict(passed=True,documents=len(files),local_links=links,json_examples=examples,
    private_paths_included=False,version=(ROOT/'VERSION').read_text(encoding='utf-8').strip()),indent=2))
