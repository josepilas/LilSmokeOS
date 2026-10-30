#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
B=${LILSMOKE_TERMINAL_TEST_OUT:-/tmp/lilsmoke-terminal-test}
OUT=${LILSMOKE_TERMINAL_SCREENSHOTS:-$B/screenshots}
mkdir -p "$OUT"
export DISPLAY=:193
export LD_LIBRARY_PATH="$B/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_VIDEODRIVER=x11 LILSMOKE_TERMINAL_WINDOWED=1
Xvfb "$DISPLAY" -screen 0 1280x720x24 -nolisten tcp >"$B/xvfb.log" 2>&1 &
server=$!
app=
trap 'test -z "$app" || kill "$app" 2>/dev/null || true; kill "$server" 2>/dev/null || true' EXIT HUP INT TERM
sleep 1
for size in 640x480 320x240; do
    export LILSMOKE_SCREEN_WIDTH=${size%x*} LILSMOKE_SCREEN_HEIGHT=${size#*x}
    "$B/host/lilsmoke-terminal" >"$B/gui-$size.log" 2>&1 &
    app=$!
    window=
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        window=$(xdotool search --onlyvisible --name '^LilSmoke Shell$' 2>/dev/null | head -1)
        test -z "$window" || break
        kill -0 "$app" 2>/dev/null || { cat "$B/gui-$size.log" >&2; exit 1; }
        sleep 0.2
    done
    test -n "$window" || { echo 'No terminal window appeared' >&2; exit 1; }
    xdotool windowfocus --sync "$window"
    xdotool type --clearmodifiers --delay 2 "printf '\\033[31mred\\033[0m \\033[32mgreen\\033[0m\\n'; tty; stty size"
    xdotool key Return
    sleep 1
    import -window "$window" "$OUT/shell-$size.png"
    if [ "$size" = 640x480 ]; then
        xdotool windowsize --sync "$window" 480 320
        sleep 1
        xdotool type --clearmodifiers 'stty size'
        xdotool key Return
        sleep 1
        import -window "$window" "$OUT/shell-resized.png"
    fi
    xdotool type --clearmodifiers 'sleep 30'
    xdotool key Return
    sleep 1
    xdotool key ctrl+c
    sleep 1
    xdotool type --clearmodifiers 'exit'
    xdotool key Return
    # Bounded wait: a broken Ctrl-C/exit must not leave the test running.
    for _ in 1 2 3 4 5; do
        kill -0 "$app" 2>/dev/null || break
        sleep 1
    done
    if kill -0 "$app" 2>/dev/null; then echo 'GUI shell did not exit' >&2; exit 1; fi
    wait "$app"
    app=
    echo "PASS SDL software surface $size, keyboard input, resize, Ctrl-C, shell exit"
done
unset LILSMOKE_TERMINAL_WINDOWED
# A stale 4:3 UI hint must not override the native widescreen display mode.
export LILSMOKE_SCREEN_WIDTH=640 LILSMOKE_SCREEN_HEIGHT=480
"$B/host/lilsmoke-terminal" >"$B/gui-fullscreen.log" 2>&1 &
app=$!
window=
for _ in 1 2 3 4 5 6 7 8 9 10; do
    window=$(xdotool search --onlyvisible --name '^LilSmoke Shell$' 2>/dev/null | head -1)
    test -z "$window" || break
    sleep 0.2
done
test -n "$window"
xdotool windowfocus --sync "$window"
xdotool type --clearmodifiers 'stty size'
xdotool key Return
sleep 0.5
import -window "$window" "$OUT/shell-fullscreen-16x9.png"
test "$(identify -format '%wx%h' "$OUT/shell-fullscreen-16x9.png")" = 1280x720
xdotool type --clearmodifiers 'exit'
xdotool key Return
for _ in 1 2 3 4 5; do
    kill -0 "$app" 2>/dev/null || break
    sleep 0.2
done
kill -0 "$app" 2>/dev/null && { echo 'Fullscreen terminal did not exit' >&2; exit 1; }
wait "$app"
app=
echo 'PASS native 1280x720 fullscreen despite 640x480 UI hints (no logical-surface stretching)'
