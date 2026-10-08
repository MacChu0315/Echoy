# Echoy current state

The user approved rebuilding from `reference/echoy-arrange-B-v3.html` with
ESP-GSP JSON/resources and shared C UI, simulator comparison, then hardware
integration. Both supplied files match `reference/SHA256SUMS`. Old implementation
history is retained, but its requirements and acceptance do not apply.

## Completed home foundation

- Official template generated under `projects/echoy`; Vibe 95469e6, utils
  fc63d43, BSP a2c985a, GSP 1.5.1 and GSPC 0.6.1. Full pins are in
  `dependencies.json`. An isolated ESP-IDF 7b9cc1a checkout is at
  `../.toolchains/esp-idf-7b9cc1a`, leaving existing SDKs unchanged.
- Initial Chinese Pulse home screen, native controls/text, procedural C Canvas
  art, recording feedback and portable recording/cancellation model.
- Model tests: 7 behavior groups pass; ASan/UBSan and leak checks pass outside
  the ptrace sandbox. Native GSP: 9 interaction/visual checks pass, including
  normal cleanup. ESP32-S31 firmware builds with zero warnings.
- Read-only code review findings were corrected and rechecked.
- Screenshots, commands and verification limits: `verification/README.md`.

## Remaining application work

Role/settings/detail/fault navigation, English UI, all twelve accompaniment
algorithms, real audio, multi-device transport/clocks and hardware integration
are not implemented. Role/settings buttons are currently visible but unwired.
The UI's output service and hardware values are explicitly simulated, including
in the buildable firmware; do not install it as a finished musical application.
No physical device write or hardware acceptance has occurred in this rebuild.

On 2026-10-08 the user approved hiding "Put away this tile (simulation)" on
hardware while retaining it in the PC simulator. Apply this when implementing
settings; that page is not implemented yet. The frozen HTML remains unchanged.

Next: implement the remaining UI and music/audio behavior using the frozen
prototype and the approved device adaptation. Continue through
simulator evidence before physical validation.
