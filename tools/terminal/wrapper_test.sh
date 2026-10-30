#!/bin/sh
set -eu
P=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
B=${LILSMOKE_TERMINAL_TEST_OUT:-/tmp/lilsmoke-terminal-test}
mkdir -p "$B/wrapper"
cp "$P/tools/terminal/fixture-terminal.sh" "$B/wrapper/terminal"
cp "$P/tools/terminal/fixture-ui.sh" "$B/wrapper/ui"
chmod 755 "$B/wrapper/terminal" "$B/wrapper/ui"
# Only substitute installation paths in an isolated copy of the real wrapper.
sed -e "s|/usr/bin/lilsmoke-terminal|$B/wrapper/terminal|g" \
    -e "s|/usr/bin/lilsmoke-ui|$B/wrapper/ui|g" \
    "$P/package/lilsmoke-terminal/src/lilsmoke-terminal-launch" >"$B/wrapper/launch"
for status in 0 1 127 139; do
    export LILSMOKE_TERMINAL_TEST_STATUS=$status
    export LILSMOKE_TERMINAL_TEST_LOG="$B/wrapper/status-$status.log"
    rm -f "$LILSMOKE_TERMINAL_TEST_LOG"
    sh "$B/wrapper/launch" &
    wrapper=$!
    wait "$wrapper"
    test "$(wc -l < "$LILSMOKE_TERMINAL_TEST_LOG")" -eq 2
    test "$(tail -1 "$LILSMOKE_TERMINAL_TEST_LOG")" = "ui $wrapper"
done
echo 'PASS wrapper restores one UI using the same supervised PID after exit/failure/crash'
export LILSMOKE_TERMINAL_TEST_HOLD=1
export LILSMOKE_TERMINAL_TEST_LOG="$B/wrapper/shutdown.log"
rm -f "$LILSMOKE_TERMINAL_TEST_LOG"
sh "$B/wrapper/launch" &
wrapper=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do
    test ! -s "$LILSMOKE_TERMINAL_TEST_LOG" || break
    sleep 0.1
done
test -s "$LILSMOKE_TERMINAL_TEST_LOG"
kill -TERM "$wrapper"
wait "$wrapper"
test "$(wc -l < "$LILSMOKE_TERMINAL_TEST_LOG")" -eq 1
echo 'PASS wrapper forwards shutdown to the terminal without reopening UI'
