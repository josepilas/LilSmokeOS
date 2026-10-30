#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
set -eu
test -n "$LILSMOKE_APP_DIR"
cd -- "$LILSMOKE_APP_DIR"
# The launcher already prepends runtime/lib and lib. Preserve its adapters.
exec ./runtime/bin/application "$@"
