# Shared Flash thresholds for horizontal startup

The user explicitly requests one common recognition-threshold set stored in
Flash, instead of volatile per-device startup calibration. Source `6542bd7`
uses the pinned `const MOSAICO_MAG_CALIBRATION_S31_V1` as the sole threshold
source. ELF inspection places its 44-byte object in `.flash.rodata`; the table is
part of the application image. All devices receive the same table. No per-device
baseline is sampled, persisted in NVS or used as authoritative threshold data.
RAM continues to hold transient samples and protocol state.

The pinned BSP's horizontal predicates use absolute left X/Y/Z and right X,
independent of the right-Y/Z baseline used for vertical edges. The adapter calls
public classifier initialization, supplies a fixed zero reference through the
public state, and suppresses every vertical result before the presence tracker.
It does not claim this reference was measured. The common numeric thresholds,
eight-sample filter, three-frame contact/release confirmation and failed-read
release remain intact. Sensor-ready logs now explicitly name fixed Flash
thresholds; no separated empty-field startup step is required for this version.

`tests/horizontal_contact/run.sh` compiles the real pinned classifier/presence
tracker with the active adapter, substituting only hardware/radio/RTOS. Old code
fails an already-joined startup assertion. The change passes all four startup
masks (none/left/right/both), short-pulse rejection, original left-reject predicate,
vertical exclusion and failed-read release. The reference table remains readonly.

All 24 normal command groups pass, including actual three-owner PCM and 51
runtime cases (`host-results.json`). Real-classifier, bounded-diagnostic and
active-adapter ASan/UBSan/LeakSanitizer checks pass. Fresh official native GSP
normal-motion verification passes seven groups with no late blocks/clipping and
exact paused silence. The same controller/UI sources and native artifacts retain
the prior reduced-motion evidence. Independent review confirms the horizontal
predicates are baseline-independent; actual flash placement is separately checked.

The pinned SDK builds normal `0baaaabeba0d` and temporary diagnostic `e793bf639647`
packages. Exact hashes/ELFs/configuration and component inspection are in their
candidate records. Accepted sound/UI bytes and retained layout are unchanged;
no Recovery/bootloader component is packaged. The diagnostic flag remains
normally off; A/f45936 temporarily receives the version with at most 90 valid
one-per-second readback records to investigate the unresolved physical BCA issue.
It will return to the matching fixed-threshold normal package afterward, not to
the superseded baseline-capturing diagnostic. All three updates are authorized
under the user's standing permission. The former `1eb36e7d795a` diagnostic was
never installed and is superseded.

This startup change satisfies the new threshold-storage requirement. It does
not by itself establish the cause or resolution of A's missing follow after a
physical rearrangement; live sensor/contact/peer readback and the actual failed
transition remain required. Installation and physical observations follow in
this directory.
