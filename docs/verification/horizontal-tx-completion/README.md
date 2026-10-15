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

## Current delivery and acceptance boundary

The TX-completion follow-up5bfaff7 is installed on all three as
0606e70d3f3c (ELF c7e6f0cb031d7c0e496938386e7261958668988271f425b527e3316d360b28b6).
The public TX callback gates submissions using static atomics; waiting consumes
neither retries nor clock stamps. Application ACKs remain required. Final25
host groups, sanitizers, fixed SDK build and matching native normal7/7 pass,
including original clock tolerance, zero PCM misses/clipping and paused silence.
Independent callback/lifecycle review is clear.

Current boots: B13347265088592221265, C15872866450408188654,
A8281715504648553660. Identity, HEALTHY/commit, retained components, fixed Flash
sampling and startup audio checks pass. Actual B—C—A graph agrees across all
three (fc2f3efc2f779672), with correct reciprocal neighbors. Empty owners are
1/2/2 (root connected, followers waiting), not recovering. Captured unified raw
submit-failure counters remain0; app retries and relay/ACK drops still occur.
C has7 cumulative MAC failures across rolling updates. No application crashes,
but zero-loss/sustained-RF or acoustic timing acceptance is not claimed.

Current user step awaiting result: keep B—C joined, move current rightmost
original A to the far left to form A—B—C, record on A, wait until the one-shot
reply finishes and hold35s. B/C must follow silently. Then perform the original
fast A-to-right move to B—C—A. The latest user message asked for status; it did
not answer this test. Do not mark test5 passed. Subsequent physical permutations,
detach retention and joined-start checks remain open. No further installation
permission or startup calibration question is required.
Evidence: verification/horizontal-tx-completion/; helpers/live results:
output/horizontal-tx-completion-20261015/.
