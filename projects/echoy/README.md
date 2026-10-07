# Echoy GSP application

This is the home UI and recording-model foundation, generated from the pinned
ESP-Mosaico Hello World template. It is not a complete musical application.

From the repository root, follow [the workspace instructions](../../README.md).
The firmware and native `pc/` backend compile the same `main/echoy_ui.c`,
`main/echoy_model.c` and `main/echoy_art.c`.

`ui/main.json` and icon/font assets are authored by `../../tools/generate_scene.py`.
Edit the generator, then regenerate; do not hand-edit its outputs. The retained
Vibe Mode integration and `main/board_display.c` come from the official template.

Audio and hardware state are currently simulated. Role/settings navigation,
other roles, English UI and multi-device operation remain unfinished.
Do not use this image as a finished device build.
