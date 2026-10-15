# ESP-NOW send completion gate

9d00b69f4fce admitted the correct horizontal graph but hardware B/C still showed
repeated ESP_ERR_ESPNOW_NO_MEM and middle relay drops. The pinned IDF
`docs/en/api-reference/network/esp_now.rst` recommends waiting for the previous
send callback before submitting another packet. The public BSP peer-link code
registers RX only; application code now owns the unused TX callback through
`esp_now_register_send_cb`, without changing SDK/BSP sources.

The Wi-Fi callback touches static atomics only: busy/completed/MAC-failed. It
never calls audio, logs, allocates or references freed board storage. The worker
checks availability before spending an attempt or stamping a clock frame.
Submit failure releases the gate; accepted sends hold it until completion.
Reliable application ACKs remain required: MAC success is not application delivery.
Stop waits for the worker, unregisters TX callback, then deinitializes peer-link;
errors retain context. The existing10ms spacing and50ms submit-error backoff,
five-attempt/2s operation bounds and musical30s deadline remain unchanged.

Tests cover delayed, synchronous and failed TX callbacks, immediate submit
failure, driver busy without consumed retries, actual six-order owner/PCM chains
and clock accuracy. Driver gate regression was red before implementation and
green after. Final25 host groups and radio/adapter/chain ASan/UBSan/LSan pass.
Independent read-only review found no open Important/Critical issue in this
follow-up; hardware success is not inferred from that review.

SDK run20261015-180647-system-update-bundle-879194 builds successfully with the
known retained-factory warning. Exact normal candidate0606e70d3f3c is in
normal-candidate.json. Accepted sound/UI/Flash profile and retained components
are unchanged. The matching native normal run passes all seven checks, original10ms clock
comparison, zero PCM late blocks/clipping and exactly silent paused output. The prior admission
normal/reduced runs passed; UI/motion sources are unchanged and no fresh reduced
run is claimed for this driver-only follow-up. Hardware delivery and evaluation
of send completion/counters remain pending.
