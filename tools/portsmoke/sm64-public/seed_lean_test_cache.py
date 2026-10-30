#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Seed only a private QA cache to test unchanged game launch without a source ZIP."""
import json
from pathlib import Path
import shutil
from cook import Cache,digest_file

source=Path('/root/sm64-public-candidate/Super Mario 64')
qa=Path('/root/sm64-public-package-lean-20261013')
target=qa/'installed/independent-apps/Super Mario 64'
assert target.resolve().is_relative_to(qa.resolve())
assert not (target/'runtime/corresponding-source').exists()
assert (source/'runtime/recipe.json').read_bytes()==(target/'runtime/recipe.json').read_bytes()
identity=digest_file(source/'runtime/recipe.json')
game=Cache(source,identity).current()
assert game is not None
destination=target/'.sm64-cache'
destination.mkdir(exist_ok=False)
(destination/game.parent.name).mkdir()
shutil.copyfile(game,destination/game.parent.name/'game')
(destination/game.parent.name/'game').chmod(0o755)
shutil.copyfile(source/'.sm64-cache/current.json',destination/'current.json')
assert Cache(target,identity).current() is not None
print(json.dumps(dict(private_test_cache_seeded=True,source_zip_on_console=False,recipe_unchanged=True)))
