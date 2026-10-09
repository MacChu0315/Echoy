# Single-device firmware preparation

The user approved proceeding after accepting the v4 GSP presentation. Preserve
the frozen HTML and shared musical/controller behavior. This work prepares and
verifies local firmware; it does not authorize a physical device write.

1. Add a TX-only ES8311/I2S adapter using public pinned BSP/SDK interfaces.
   Report partial writes, bound waits and clear queued sound on cancellation.
2. Read real battery status on a separate worker. Failed/missing readings are
   unknown, not a simulated percentage or a false empty-battery condition.
3. Connect the shared band to FreeRTOS. One task owns control and PCM scheduling;
   immutable synthesis jobs run on a lower-priority worker. GSP only enqueues
   actions and copies a cached view. Startup/recovery remain silent.
4. Preserve Recovery and partitions. Build and inspect the official system-update
   bundle, including hashes and its actual component offsets.
5. Exercise adapters with host fakes, shared controller regressions and isolated
   GSP checks. Physical sound, timing, resource watermarks and battery behavior
   require later device acceptance. Wireless linkage and hardware export remain
   separate unfinished work.

Before installation, present the concrete artifact, identified target and write
scope and obtain the user's confirmation. Never use generic IDF flash targets:
the retained Recovery layout requires the official Mosaico update workflow.
