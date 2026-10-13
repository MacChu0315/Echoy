# Home art producer checks

Run `sh tests/art_producer/run.sh`. The first executable uses the actual pthread
worker and rasterizer. The second compiles the same producer state machine's
ESP branch against small pthread-backed FreeRTOS fakes. Neither starts GSP,
audio, a simulator session or hardware.

The tests hold a real raster call for 200 ms while the owner replaces requests,
then compare delivered RGB565 buffers with isolated rasterization. They cover
copied input/workspace ownership, same-epoch progress coalescing without starving
completed frames, structural invalidation, final static output, submission
errors, borrowed-buffer preservation, immediate/delayed/concurrent release,
allocation/task failures and retryable stop/destruction.

The ESP fakes additionally reject each non-PSRAM allocation and verify the
12288-byte `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` task request. They hold a
quiescent worker before suspension to prove that external deletion waits for
`eSuspended`, then check matching `vTaskDeleteWithCaps` and self-only stack
watermark reads. These checks do not measure actual SDK heap or stack margins.

For sanitizers, retain default LeakSanitizer and use:

```sh
ART_PRODUCER_CFLAGS='-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' sh tests/art_producer/run.sh
```

In environments that run sandboxed processes under ptrace, execute this local
test outside that sandbox instead of disabling leak detection. The native GSP
UI integration tests and pinned-SDK build remain separate required checks.
