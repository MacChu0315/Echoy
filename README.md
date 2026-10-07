# Echoy for ESP-Mosaico

Echoy is being rebuilt from a new HTML prototype that the user will supply.
No prototype has been selected for the rebuild yet.

The existing source, tests, assets, reference fixtures, and build directories
remain as legacy material. The HTML under `reference/` belongs to the previous
implementation; it is not the specification for the rebuild. Do not select a
prototype from another directory without the user's direction.

Previous handoff notes, design documents, acceptance records, and generated
run artifacts have been removed from the working tree. Old requirements and
validation results do not establish requirements or acceptance for the rebuild.
Git history is retained.

## Retained layout

- `main/`: legacy ESP-IDF application entry point.
- `components/`: legacy music, audio, UI, and hardware integration code.
- `pc/` and `tools/`: legacy desktop runners and development tools.
- `tests/`: existing automated tests and fixtures.
- `assets/`: application assets and font licenses.
- `reference/`: previous HTML prototype and reference fixtures used by the tests.

The next step is to inspect the supplied prototype and define the new
application behavior, dependency versions, and validation requirements.
