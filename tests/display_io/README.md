# LCD queue policy checks

`sh tests/display_io/run.sh` checks the production constructor wrapper's
two-slot LCD limit, unchanged caller configuration, narrower existing queues,
other buses/panels and error propagation. A separate archive-order check ensures
the wrapper is extracted before the BSP introduces its SDK reference.

The host substitutes the SDK constructor; it does not simulate SPI DMA allocation
or prove a byte-level heap budget. The pinned BSP requests eight-line RGB565
chunks, and the SDK rounds transfer capacity/alignment. Two slots mean roughly
16 KiB of staging payload, plus descriptor/allocator overhead. Verify the actual
ELF call path, error-free transfers and the physical LCD across warm/cold starts
on hardware. A shadow screenshot alone does not verify panel presentation.
