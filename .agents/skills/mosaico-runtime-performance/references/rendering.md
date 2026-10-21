# Native rendering and measured performance

Read this for a slow dynamic Canvas, sensor-follow animation or memory-heavy
pixel renderer. Preserve the approved reference and quantify any visual change.

## Establish what was counted

Record input scene/state, source/ELF/config/resource identity, boot and observation
window. Separate complete input samples, UI timer callbacks, tiled Canvas draws,
engine-completed frames, Host commit sequences and physical panel-visible FPS.
A callback can cover only a tile. Movie encoding rate is not display throughput.
Compare normal preview separately from screenshot/RPC polling conditions.

Separate preparation, raster, submission and waits. A faster shader cannot
remove a submission floor; wall time includes preemption and is not CPU cycles.
Include slow transitions and cache misses. Report active open-eye/complex states
separately from cheap static/closed ones; a higher mixed-state mean does not meet
a target defined for active following. If statistics describe a completed prior
generation, classify using that generation's pose, not the newly prepared pose.
Use meaningful fixture tests for transition accounting before trusting a counter.

## Optimize without changing the accepted output

Measure avoidable work: invisible coverage, repeated geometry, expensive integer
width/division, sampler boundaries and structure copies. Compiler/assembly
inspection can motivate a candidate; it does not establish a device speedup.
Retain high-dynamic-range intermediate values and prove arithmetic bounds before
changing representations or replacing divisions. Keep strict math when exact
reference pixels are required; no casual fast-math or reduced texture quality.

Classify invariants by lifetime. Static geometry can have an immutable cache;
pose-dependent geometry can be prepared for an exact key. Include every actual
dependency; gaze/pupil must remain dynamic where required. Cache invalidation
and allocation failure need tests. Test cached vs scalar full frames, extreme
poses, closure/aperture, odd tile origins, stride/padding and edge regions.
Prepare owns cache updates; draw consumes an immutable prepared generation.
Changing geometry can invalidate reuse so model the worst-case cost, not only
a favorable stable microbenchmark.

PSRAM is not free: count workspace, each cache, XIP/rodata reservations, textures
in the app and UI bundle separately, framebuffers, driver/DMA storage and peak
overlap. Zero texture decoding does not mean zero PSRAM. Use the supported
capability-specific allocator; propagate failure instead of silently spending
internal memory. Target ABI/map/config measurements outrank Host sizeof/RSS.

## Approvals and stopping points

Keep exact-output changes distinct from detail reduction or motion-policy
changes. A concrete side-by-side/reference and current user decision determine
whether a lower-detail trial is permitted; one approved reduction does not
authorize arbitrary further reductions. Do not tune detection thresholds to
hide a rendering bottleneck.

Different hand-played scenes show an observed rate, not a controlled percentage
speedup. Preserve the before/after workload and accepted restore artifact where
available. If the user stops at an improved but below-target version, state both
the stopping decision and the unmet metric. Keep uninstalled experiments separate
from installed source and acceptance; do not install another candidate after a
request to retain the current version.
