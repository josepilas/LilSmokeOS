#!/bin/sh
# Replaces the historical transfer-image test with actual two-partition tests.
set -eu
cd "$(dirname "$0")/../.."
exec unshare --mount --propagation private python3 tools/hwtest/storage_layout_smoke.py
