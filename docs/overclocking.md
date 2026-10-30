# LilSmokeOS overclocking

The SF3000 stock boot log reports a 1,188 MHz CPU. Settings exposes `stock`,
`1300MHz`, and `1400MHz`, but the latter two are requests, not a promise that
the board supports those rates.

## Safety contract

`/usr/lib/lilsmoke/lilsmoke-overclock` only uses Linux CPUFreq sysfs. It never
writes PLL, clock-controller, voltage-regulator, or bootloader registers.

An overclock request is applied only when:

1. `/sys/devices/system/cpu/cpu0/cpufreq` exists;
2. the requested frequency is advertised by the active CPUFreq policy; and
3. a thermal zone is readable, unless a developer explicitly enables the
   dangerous `CPU_ALLOW_UNMONITORED_OVERCLOCK=1` override.

The current open HC16xx kernel has common-clock support, but no validated
CPUFreq/OPP/voltage/thermal stack for the SF3000. Consequently 1.3/1.4 GHz is
expected to remain unavailable on real hardware for now. This is correct and
safe behavior.

## Runtime behavior

```sh
lilsmoke-overclock apply
lilsmoke-overclock watch
lilsmoke-overclock status
```

`watch` drops the ceiling to `CPU_STOCK_MAX_KHZ` (default `1188000`) at or
above `CPU_THERMAL_THROTTLE_C` (default 82 C), and restores the selected rate
at or below `CPU_MAX_TEMP_C` (default 78 C). If required kernel interfaces are
missing, it logs the reason and leaves the hardware untouched.

## Required future work

- identify the exact HC16xx CPU PLL and regulator relationship;
- add a board-tested CPUFreq driver and OPP table;
- expose a calibrated thermal sensor and cooling policy;
- validate voltage, charging, low-battery, suspend, and warm-room behavior;
- run long SNES/PS1 stress tests on every supported revision.

Do not infer safe PLL values from JZ4770 code or from another HC16xx product.
Only frequencies exposed by a validated kernel policy should reach userspace.

Useful kernel references:

- <https://docs.kernel.org/admin-guide/pm/cpufreq.html>
- <https://docs.kernel.org/power/opp.html>
