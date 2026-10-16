# Skill package validation

Historical record; see the [2026-10-16 consolidation checks](../../docs/skills-consolidation.md) for current whole-package structural coverage. Skill bodies and references are unchanged.

Date: 2026-10-15. Scope: five standalone skill documents and one optional record
template. No application code, SDK, firmware, device or production dependency is
changed by this package.

## Scenario checks

A fresh agent first answered five scenarios without loading the candidate
skills or project source. Its baseline already rejected swallowed UI errors,
unsupported three-device acceptance, unauthorized device writes and contaminated
acoustic evidence. This is not an all-failing baseline and does not establish a
measured improvement in general agent reliability.

The runtime baseline proposed "shorten lock-held work" but did not explicitly
bound publication independently of reader preemption. It also did not address
borrowed display-buffer lifetime. The candidate documents make those conditions
explicit, based on the historical implementation failures.

Two fresh-context agents then read the relevant skills and applied them to the
following supplied scenarios. They did not receive this rubric, the baseline
answers or the author's intended solution. All checks below were manually
reviewed against their actual answers.

| Scenario | Observed application of the skill |
| --- | --- |
| Normal-battery simulation passes; unknown startup fails hiding a generated object with no properties | Inspect compiled visibility, fix source/generator, preserve errors, exercise unknown/known transitions with real callbacks, isolate active preview artifacts |
| Role click coincides with a slow snapshot reader and audio deadline miss | Separate wait from render cost; use nonblocking optional publication, retain reliable commands; shortened reader locks alone do not bound preemption |
| Hidden images retain internal memory; PSRAM is available | Inspect lifetime and capability-specific allocation; do not move DMA/stack resources blindly |
| Worker buffer is borrowed by UI; cancel arrives and flush times out | Retain valid immutable storage/context until release; cancellation is not release permission |
| Three desktop tiles share one clock; physical adapter has one peer | Do not claim physical three-device capacity; check bidirectional middle-node requirements and independent clocks |
| Local contact asserted during radio loss; peer paused | Separate physical contact from radio freshness; preserve local Pause and resolve product-specific recovery semantics |
| Unknown update outcome, changed port, HEALTHY boot with wrong ELF | Read-only reconciliation and live identity; no successful installation claim or automatic rollback/retry |
| Skills-only publication from a branch with private application ancestry | Export an explicit allowlist into a separate checkout; do not push application ancestry |
| Digital headroom, speech-contaminated microphone windows, exhausted budget, mismatched current plan | Continue authorized offline analysis; preserve budget and accepted roles; label reconstructed plans and acoustic uncertainty |

Negative routing also passed: browser-only CSS, compiler-only failures and
generic CRUD review do not trigger these specialist workflows. Mosaico-specific
offline audio analysis may use the audio guide without enabling capture or
device operations. The release template permits omitting irrelevant fields and
reusing existing approval; it does not impose per-field user confirmation.

The additional buffer-lifetime case was forward-only, not a paired baseline
experiment. These are finite qualitative application checks, not a statistical
benchmark or proof of correctness for every future task.

## Structural checks

The package is checked with the skill-creator `quick_validate.py` for each skill,
plus local checks for matching directory/name, bounded nonempty descriptions,
balanced Markdown fences, resolvable relative links and portable file contents.
Repository whitespace checks and frozen-reference hashes are checked separately.
The copied export is validated again before publication.

The structural checker is development tooling, not a runtime dependency bundled
with these guides. The package adds no executable scripts whose behavior would
require application testing.

## Boundaries

- No new firmware build, simulator run, installation, recording or hardware
  acceptance is claimed. Historical Echoy results motivate the techniques.
- Exact APIs, SDK versions, flash layouts and supported CLI flags remain the
  target project's responsibility; the guides deliberately do not invent them.
- First-run microphone permissions, product semantics and physical capacity
  cannot be inherited from the source project.
- No claim is made that source-project historical failures are reproduced by
  this documentation-only validation.
