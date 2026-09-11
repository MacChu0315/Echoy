# Task 1 report: executable Echoy reference

## Result

- Pinned the exact `Echoy_v5.0.3.html` input at SHA-256
  `d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5`.
- Extracted the original `DuijuResponse`, `DuijuHarmony`, `DuijuEngine`, and
  `DuijuSession` source by unique named boundaries. The manifest records the
  source identity, exact module sizes/hashes, embedded version 5.0.4, and the
  three reference repository commits.
- Added a Node VM oracle with 18,360 music cases and 81,120 expected events.
  The matrix crosses four roles, three phrases, 17 role-specific activity
  samples, five bars, six left-neighbor motifs, and duplicate 0/1/2.
- Added six executable session trajectories with 21 checkpoints for discovery,
  joining, beat-start, release/clock retention, motif/output separation, and
  committed role/phrase/activity state.
- Added 19 UI states in Chinese and English. The 38 strict 480x480 PNGs are
  accompanied by hashes, captured product actions, hit rectangles, selected
  computed styles, frozen font hashes, canvas checksums, and each frame's
  actual motif/score steps.

## RED / GREEN evidence

Initial characterization before expanding the contract:

```text
$ node tests/oracle.mjs --self-test
{"ok":true,"sha256":"d23c...bb5","events":8}
```

RED after adding the required artifact contract:

```text
$ node tests/oracle.mjs --self-test
AssertionError: missing generated reference artifact:
tests/cases/music-fixtures.json
exit 1
```

RED after adding exact extracted-module checks:

```text
$ node tests/oracle.mjs --self-test
AssertionError: response manifest entry lacks extraction metadata
exit 1
```

GREEN for deterministic fixture generation:

```text
$ node tests/oracle.mjs --generate
fixtureCases: 18360
fixtureEvents: 81120
trajectories: 6
checkpoints: 21
uiCases: 38
exit 0
```

The first browser launch inside the restricted filesystem/process sandbox was
kept as environment evidence:

```text
$ /usr/bin/google-chrome --headless=new ... --remote-debugging-port=0
ERROR:third_party/crashpad/crashpad/util/linux/socket.cc:45
setsockopt: Operation not permitted
exit 133
```

The same local-only capture tool was then run with the host browser permission:

```text
$ node tools/capture_reference.mjs --generate
uiCases: 38
browser: Google Chrome 151.0.7922.75
exit 0

$ node tools/capture_reference.mjs --check
uiCases: 38
browser: Google Chrome 151.0.7922.75
exit 0
```

`--check` performed a second full browser capture without overwriting the
committed artifacts and compared all metadata and image hashes.

Final GREEN verification:

```text
$ node tools/extract_reference.mjs --check --self-test
ok: true; mode: check; selfTest: true; source and four module hashes matched

$ node tests/oracle.mjs --generate
18,360 cases; 81,120 events; 6 trajectories; 21 checkpoints; 38 UI cases

$ node tests/oracle.mjs --self-test
ok: true
deterministicSha256: ffb32c8ce14a42fd02cd6deb6360e41e0f1aadabc3419d4a73e2d49701c990fc

$ node --check tools/extract_reference.mjs
$ node --check tools/capture_reference.mjs
$ node --check tests/oracle.mjs
$ git diff --check
all exited 0
```

## Screenshot evidence

- 19 `zh-CN` and 19 `en` images are listed in `reference/ui/manifest.json`.
- Every PNG is exactly 480x480 and has a verified SHA-256.
- Manual visual review covered Chinese Pulse performance, English Harmony
  performance, Chinese Harmony role/phrase selection, and the English volume
  modal. Text, clipping, rounded corners, instrument graphics, dimmed backing
  page, and selected states were visible as expected.
- `tests/cases/ui-capture.json` ties every dynamic canvas to its captured motif
  and records identical score-step and motif-step lists.

## Limits

- UI pixels are pinned to Chrome 151.0.7922.75 and the six font files/hashes
  listed in `tests/cases/ui-capture.json`. A host missing those exact inputs
  fails before capture rather than silently producing a new baseline.
- Browser capture requires permission to start Chrome where process sockets are
  restricted. Fixture generation and `node tests/oracle.mjs --self-test` do not
  launch a browser.
- These files establish the HTML/JS oracle. They do not claim firmware build,
  LVGL equivalence, acoustic behavior, wireless timing, or device acceptance.
