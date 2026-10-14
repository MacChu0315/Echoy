# Rejected startup magnetic readback

Run `sh tests/magnet_diagnostics/run.sh` from the repository. It compiles the real
`board_link.c` sample path in enabled and disabled configurations. Only OS/radio,
sensor values, classification outcome and log output are test adapters.

The test fails before the production logging change. It checks successful BSP
read outputs reach the correct LEFT/RIGHT fields, -32768 gets an exact sentinel
bit while another saturated value does not, one sensor window cannot duplicate
logging, three rejected windows still disable linkage, accepted startup produces
no rejected-window records, and failed reads never emit fabricated axes. Default
disabled builds emit no diagnostic records and keep the same failure behavior.

For ASan/UBSan/LeakSanitizer outside ptrace:

```
MAG_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
sh tests/magnet_diagnostics/run.sh
```

Values are compensated BMM150 outputs of the final sample in each rejected
window, not raw ADC registers or the classifier's eight-sample average. A
sentinel match identifies an output marker without establishing its physical
or trim/compensation cause. No host result proves actual device axes.
