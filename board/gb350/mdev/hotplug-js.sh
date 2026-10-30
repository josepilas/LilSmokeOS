#!/bin/sh
#
# LilSmokeOS mdev helper — joystick hotplug
#
# Called when a /dev/js* device appears. Triggers LilSmokeUI to reload
# its input device list (via SIGUSR1).

# Touch a file in /run that LilSmokeUI watches for
touch /run/js-hotplug.flag
pkill -USR1 lilsmoke-ui 2>/dev/null
exit 0
