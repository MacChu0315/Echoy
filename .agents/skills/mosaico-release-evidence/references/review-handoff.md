# Small review handoff

Keep artifact truth simple: source implementation commit, final evidence/docs
commit, built ELF/resources and actually running version may differ. State each
role explicitly. Source matches are verified from actual files/artifacts, not
inferred from the latest Git commit or a package name. Preserve accepted restore
packages and their supported official command; rebuilding is not an exact backup.

A review archive should include runnable application source, dependency pins and
licenses/provenance, necessary small raw inputs, conversion/generator commands,
actual outputs, important logs, dynamic evidence and current limitations. Exclude
.git, caches, credentials, retrievable dependency trees and large redundant local
backups. Do not import another project's .git. Use an explicit manifest/allowlist
when exporting from a product tree. Sharing to a remote destination still needs
the applicable authorization.

Hash archive contents and verify that the ZIP extracts and passes its documented
Host build/replay commands without accidentally reading the original working
tree. Identical evidence may be aliased only by matching file hashes with retained
state/provenance; visually similar captures are not interchangeable. A recorded
frame trace and its expected outputs must remain distinguishable.

For user acceptance, describe what was actually observed. “Keep it for now” is
a stopping decision, not proof of an unmet performance threshold, unobserved
physical transition or completed sustained-load test. Keep failed/blocked and
unmeasured results visible. Uninstalled WIP may be retained on a separate local
branch with its validation limits; it is not the delivered firmware.

When the task is to extract reusable skills, include only transferable guidance
and its licenses/source commits. Keep device IDs, standing write approvals,
thresholds, artwork and private recordings out of generic defaults. Preserve
experimental/not-run status and distinguish old validation from new checks.
