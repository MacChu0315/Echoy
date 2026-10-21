# mosaico-dev-skills

- This repository contains reusable guidance and offline validation, not Mosaico application source or device tooling. Skills do not grant device/remote write permissions or inherit source-project tuning/identities.
- Keep each skill self-contained; conditional references stay inside its directory. Select by actual task. Preserve the three experimental skills under `skills/` and their experimental/not-run metadata until their own evaluations run.
- `.agents/skills/` on `skills/mosaico-development` is the engineering source of truth. Consuming projects link whole skill directories from a pinned submodule; do not independently edit linked copies or silently update their gitlink.
- Preserve source/license attribution in `docs/SOURCES.md`. Separate historical source-project evidence, structure tests, agent scenarios and hardware results. A user stopping short of a target does not turn it into a pass.
- Validate with `python3 -m unittest discover -s tests -v`; retain meaningful submodule clone/link and skill portability checks when integration changes. No shared SDK upgrades or hardware operations for documentation validation.
- Current status: `docs/PROJECT_STATE.md`. Consumer integration and the existing skills-branch distribution route: `docs/INTEGRATION.md`.
