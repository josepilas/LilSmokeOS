#!/bin/sh
printf 'terminal %s\n' "$$" >>"$LILSMOKE_TERMINAL_TEST_LOG"
if [ "${LILSMOKE_TERMINAL_TEST_HOLD:-0}" = 1 ]; then
    trap 'exit 0' TERM
    while :; do sleep 0.1; done
fi
exit "${LILSMOKE_TERMINAL_TEST_STATUS:-0}"
