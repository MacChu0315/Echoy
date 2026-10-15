# Reusable Mosaico development skills

Five focused guides extracted from Echoy's prototype, simulator and physical
device development. Each skill is usable independently in another Mosaico
project. They contain techniques and verification boundaries, not Echoy firmware,
device identities, production tuning values or historical write authorization.

## Select by task

| Skill | Use for | Boundary |
| --- | --- | --- |
| [mosaico-gsp-parity](mosaico-gsp-parity/SKILL.md) | Approved prototype → shared C/GSP, generated capabilities and real native input | Supplements existing UI/simulator workflows; no browser-only design |
| [mosaico-runtime-performance](mosaico-runtime-performance/SKILL.md) | Touch stalls, audio deadlines, ownership and internal-memory pressure | Measure the full path; host tests do not establish device timing |
| [mosaico-peer-cooperation](mosaico-peer-cooperation/SKILL.md) | Contact, radio, membership, clocks, shared data and local permissions | Product semantics and physical topology remain project decisions |
| [mosaico-audio-validation](mosaico-audio-validation/SKILL.md) | Reference DSP, actual PCM and physical sound | Numerical correctness is separate from acoustic acceptance |
| [mosaico-release-evidence](mosaico-release-evidence/SKILL.md) | Exact artifacts, uncertain updates and accepted-version records | Supplements device operations; grants no write permissions |

These complement the existing Vibe `mosaico-ui`, `gsp-sim`,
`mosaico-device-operations` and build guides. They neither copy those skills nor
require their installation. Each target repository's maintained commands, pinned
dependencies, public integration contract and user instructions remain the
source of operational details. Load only the guide relevant to the task.

## Reuse in another project

Copy the selected complete skill directory into that project's `.agents/skills/`.
Keep `references/` with `mosaico-release-evidence`. Each `SKILL.md` includes its
own name, task triggers and instructions; the index and validation record are
not runtime dependencies. Existing directories with the same name need review
before replacement. Global installation is optional and is not performed by
this package.

For example, ask an agent to use `mosaico-runtime-performance` to investigate a
touch stall while audio plays. Supply that project's logs, source and version
information. A runtime that supports explicit `$skill-name` invocation can use
`$mosaico-runtime-performance`; otherwise provide the installed `SKILL.md` path.

No executable helper, new production dependency or additional tool service is
required. The release skill includes an optional
[acceptance record](mosaico-release-evidence/references/acceptance-record.md).

## Scope and provenance

The techniques derive from the Echoy GSP implementation and subsequent physical
feedback, particularly commits `90485c9` (compiled visibility), `8552b2e`
(nonblocking audio publication), `443dbfa` (memory/latency), `d4ac5dc`
(background artwork), `05a2e6b` (bounded measurement), `0953c52` (accepted sound)
and `cca129d` (cooperation). These identify the source project's history;
the skills-only distribution intentionally does not include that history or
its hardware evidence. The guides are self-contained and do not require access
to those commits.

Echoy-specific layouts, musical rules, sample rates, gains, device IDs, SDK
revision numbers, flash offsets, attempt budgets and physical capacity are not
defaults for other applications. Confirm them in each target project.

See [validation](VALIDATION.md) for application scenarios, checks and limits.
