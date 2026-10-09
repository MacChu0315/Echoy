# v4 merged home controls

The user approved the v4 prototype migration after reviewing the change list.
Its musical core, audio, arrangement and existing action methods match v3.
Keep them; update the GSP/C presentation and the input regression coordinates.

- Archive the delivered v4 HTML and metadata separately, with hashes. Preserve
  v3 and the original ZIP; never edit either baseline to satisfy tests.
- Match the one-line identity, 480x480/R60 screen, raised 300x204 pad at (90,89),
  moved sliders/progress/feedback, compact state line and 84/244/84 bottom row.
- Idle: independent 122x84 Record/Play targets. Capturing: one 244x84 Cancel.
  Preview/loop and their start preparation: one 244x84 Pause. Preserve permissions
  when only a new parameter plan is preparing; do not invent an extra play state.
- Switch hit areas immediately. Stale/inactive actions cannot start recording
  or playback; visual transitions do not gate cancellation or audio stopping.
- Match 240ms visual merging and interruption, with a reduced-motion path. Verify
  actual GSP interpolation against the HTML, not merely final screenshots.
- Remove steady instruction text; retain meaningful count-in, capture, pending,
  finite-preview and error/cancellation messages in the compact state line.
- Re-run native pointer/strip, audio, linked desk, language and fixed-SDK checks.
  Replace only the owned preview after validation; do not flash or write remotes.

Completed: archived v4, native merged controls and exact CSS presentation math,
interruptible inputs, reduced-motion propagation, reference/native captures,
audio and three-tile regressions, fixed-SDK build. The user confirmed touch/mouse
acceptance and excluded new PC keyboard focus mapping. Results are recorded in
`docs/verification/v4/README.md`.
