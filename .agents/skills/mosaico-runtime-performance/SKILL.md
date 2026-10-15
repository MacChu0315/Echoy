---
name: mosaico-runtime-performance
description: Use when Mosaico UI, touch, audio or concurrent device work stalls, misses deadlines or exhausts internal memory. Not for cosmetic changes or compiler-only failures.
---

# Mosaico runtime performance

Measure the path responsible for the reported symptom before changing deadlines,
priorities or allocation policy. Use the project's pinned SDK and public BSP
interfaces. Device measurement and installation remain subject to task scope.

## Separate execution cost from waiting

Associate observations with device identity, Boot ID, firmware/configuration and
input. An error page with an unchanged boot is not evidence of a restart.
Measure relevant stages separately: input arrival/read, queue residence, model
work, mutex wait, rendering, encoding, PCM conversion and output submission.
Distinguish callback time, callback gaps, frame rate and physical input latency.
An idle inter-read gap is not necessarily input latency.

Build a compact table of each task's owned mutable state, blocking operations,
deadline and buffer lifetime. A fast rendering microbenchmark cannot rule out
lock contention or resource retention in the complete path.

## Protect the owner that has a deadline

For optional state publication, a slow UI/diagnostic reader must not stall the
audio owner. A nonblocking publication attempt can retain the last coherent
snapshot and retry next block; count deferrals. Keep commands and essential
state transitions on their reliable path: do not silently drop them as snapshots.
Shortening a reader's critical section alone does not bound preemption time.

Move expensive preparation away from the UI/touch or output owner when measured
cost warrants it. Copy immutable requests; identify obsolete completions with
generations. Coalesce animation without starving completed frames or the final
static result. Keep one UI/touch owner. Borrowed display buffers stay immutable
until release; failed stop/flush retains live context until safe cleanup/retry.

## Account for memory by capability and lifetime

Measure internal free/largest block, PSRAM, task stack watermarks and retained
resources under the failing workload. Hidden images may retain encoded sources;
an externally controlled UI may still allocate an unused standalone model.
Move eligible bulk storage to explicit PSRAM allocations using supported APIs.
Check DMA/access/stack constraints before moving an object; abundant PSRAM does
not imply internal allocations can succeed. Avoid global malloc-policy changes.

## Verify the fix

Inject the observed slow reader, blocked worker, allocation failure or shutdown
race into production-code host fixtures. Re-run the original input sequence and
affected output/cleanup regressions, then measure hardware under comparable load.
Retain numerical or pixel equivalence budgets where behavior must be preserved.
Do not relax a deadline to hide an unexplained miss.

Report baseline and candidate identity, stage measurements, resource margins,
behavior checks and remaining uncertainty. Different hand-played inputs support
an observed improvement, not a controlled percentage speedup. Host tests cannot
establish FreeRTOS scheduling, PSRAM bandwidth or physical DMA margins.
