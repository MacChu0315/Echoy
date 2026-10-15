# Delivery and acceptance record

Use only applicable fields. Keep this record local unless its destination and
contents are authorized for sharing. Never include credentials or raw personal
recordings. Device identity can be omitted from public summaries while retained
in authorized local evidence.

## Artifact

- Source revision; relevant dirty files and their hashes, if any:
- Toolchain/dependency revisions and effective configuration:
- Build command, exit result, warnings and log location:
- Package path/hash; application ELF/image and UI resource hashes:
- Package components, partition compatibility and retained firmware checks:
- Known accepted artifact available for an authorized restore:

## Operation

- Authorized operation, target and scope:
- Live device identity, mode and pre-operation Boot ID:
- Exact submitted artifact; operation ID and final/reconciled result:
- Post-operation identity, Boot ID, running image/resources and health:
- Retained component comparison, including hash region/padding conventions:
- Current-boot calibration/readiness if relevant:

## Acceptance

| Claim | Evidence/command and result | Version/boot | Provenance and limits |
| --- | --- | --- | --- |
| Build/install/startup/functional/experience | Record actual evidence | Identify tested version | New test, historical result or user report |

- Unresolved failures and excluded physical conditions:
- Next action within remaining authorization, or decision required:

Do not fill missing evidence with assumed success. Reuse valid prior approval;
record it rather than treating every field as a new user confirmation gate.
