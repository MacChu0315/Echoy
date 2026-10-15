---
name: mosaico-gsp-parity
description: Use when translating an approved prototype into a Mosaico GSP application or investigating differences between prototype, native simulator and device UI. Not for browser-only styling or initial visual design.
---

# Mosaico GSP parity

Preserve the approved experience while checking the actual generated resources
and shared C implementation. Read the target project's rules and pinned GSP
tooling; resolve commands from its installed CLI help and public preview guide.
Existing design approval counts. This guide does not add a design approval gate
to an already specified change or authorize device writes.

## Map the reference to executable behavior

Keep the supplied reference immutable and record its hash. Record approved
departures separately. For each affected feature, identify its reference state,
C handler/model, generated binding, relevant input sequence and expected result.
Keep coordinates, text, typography, hit targets and behavior distinct: a matching
picture does not establish a working control.

Share portable C UI and business logic between native sim_bridge and firmware.
Keep board, FreeRTOS and transport dependencies in platform adapters. A browser
may provide an outer desk or test controls, but must not replace the product
logic being accepted. For algorithmic ports, compare actual C outputs against
the frozen reference over representative and boundary inputs.

## Verify compiled capabilities and inputs

Inspect the generated API descriptor, not only source JSON. An object name does
not imply visibility, text or input operations. If an operation fails, fix its
source declaration/generator and regenerate; preserve meaningful error returns.

Example: normal battery tests never hide the fill. An unknown startup battery
does. A named fill with `properties: []` needs an explicit supported visibility
binding, then an unknown → known → unknown regression through real C callbacks.

Run the matching official simulator with the application backend. Scene-only
captures do not execute callbacks; inspect the installed wrapper's mode selection
before choosing flags. Drive down/move/up and held input as applicable. Cover
cancelled asynchronous work, stale input after transitions, missing data and
fault recovery when affected. Wait for observable readiness or transition
completion with a bounded deadline, rather than a guessed sleep.

Snapshot the executable, resource bundle and API descriptor for each acceptance
run; check that they belong together. Use separate build directories/ports for
concurrent previews and never rebuild an active preview's bundle in place.

## Report the boundary

Compare equivalent states at the device resolution, including relevant languages
and reduced-motion settings. Report executed input flows, localized visual
differences and unresolved device checks. Simulator callbacks cannot establish
physical touch latency, battery behavior, speaker output or wireless timing.
