#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
# Qualification fixture only. Records known protocol values, never host env.
set -eu
printenv LILSMOKE_APP_ID LILSMOKE_APP_NAME HOME LILSMOKE_APP_DIR
printf 'argc=%s\n' "$#"
for argument do printf 'arg=%s\n' "$argument"; done
printenv core rom LILSMOKE_OPEN_CORE LILSMOKE_OPEN_ROM || true
