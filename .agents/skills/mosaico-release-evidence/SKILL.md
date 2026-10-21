---
name: mosaico-release-evidence
description: Use when preparing, verifying or preserving a Mosaico firmware delivery and its acceptance evidence, especially uncertain updates or mismatched image/resource identity. Not for build-only work or permission to install firmware.
---

# Mosaico release evidence

Associate acceptance with an exact artifact and observed device, rather than a
filename, serial port or successful upload. Follow the target repository's
official device workflow and existing task authorization. This guide provides
evidence requirements; it grants no device or remote write permission.

## Prepare a reviewable artifact

Record source revision and relevant uncommitted changes, configuration, dependency
revisions, build command/result and component hashes. Inspect the package and
actual partition layout with official tools. Distinguish a file hash from a
flashed-region hash that includes padding. Hash validation is not a publisher
signature. Preserve the accepted package before replacing an installed version.

Use the public integration contract and `mosaico.py` workflow for retained Vibe
Mode/Recovery. Resolve supported commands from the installed version's help and
project guides; do not copy offsets or generic IDF flash commands from examples.
Include separately packaged UI resources in identity checks.

## Follow the operation through to the intended application

Before a write, verify the target identity, current mode/boot, layout, ownership
and exact artifact against the authorization. A changed port is not a different
authorized device. A previous project's device IDs or approvals are not inputs.

If a write reports an uncertain outcome, use read-only operation reconciliation,
live identity, logs and inventory before deciding whether another write is
necessary and authorized. Do not retry, recover or roll back merely because a
timeout occurred. Preserve available crash evidence before destructive actions.

Verify the intended application and resources, new Boot ID when a reboot is
expected, health/commit evidence, retained components and actual affected flows.
Treat calibration readiness as a separate current-boot check when sensors need
it. A HEALTHY boot of another ELF does not validate the reviewed package.

## Record acceptance at its actual level

| Level | Required evidence |
| --- | --- |
| Build | Successful command and identified output |
| Installation | Operation result and expected running image/resources |
| Startup | Health, current-boot logs and usable application entry |
| Functional | Exercised inputs, output and affected recovery behavior |
| Experience | User listening/usability observations with stated scope |

Use the [record template](references/acceptance-record.md) for a complete delivery;
omit irrelevant fields for smaller tasks. Distinguish new tests, historical
results and user reports. Preserve limitations such as unmeasured latency,
untested topology or sustained load. Keep current state concise and link detailed
history, so stale README claims do not contradict the accepted version.

When sharing a skills-only package, export an explicit file allowlist into a
separate checkout. Inspect its diff/history before pushing; an otherwise small
commit on the application branch can expose the entire application ancestry.

For a compact archive or user-chosen stopping point, read [review handoff](references/review-handoff.md); installed source and evidence commits can differ.
