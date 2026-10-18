#!/usr/bin/env python3
"""Deterministic tests for UI scheduling and unchanged profiled QOI pixels.

Native presentation is checked separately with check_full_ui.py against an
isolated official simulator build. These tests assert ordering, not host speed.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path,
                        default=ROOT / 'projects/echoy/build-sim-cooperation-v2')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    main = ROOT / 'projects/echoy/main'
    component = ROOT / 'projects/echoy/managed_components/espressif__esp-gsp'
    flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
             '-ffunction-sections', '-fdata-sections', '-I' + str(main),
             '-I' + str(args.build_dir.resolve() / 'echoy_backend_assets'),
             '-I' + str(component / 'tools/sim_bridge/include'), '-I' + str(component / 'include')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    with tempfile.TemporaryDirectory(prefix='echoy-ui-responsiveness-') as directory:
        executable = Path(directory) / 'test-ui'
        subprocess.run(['cc', *flags, str(ROOT / 'tools/test_ui_responsiveness.c'),
            *[str(main / file) for file in ('echoy_model.c', 'echoy_music.c', 'echoy_strip.c',
                                          'echoy_display.c', 'echoy_art_producer.c', 'echoy_art.c')],
            '-Wl,--gc-sections', '-pthread', '-lm', '-o', str(executable)], check=True)
        for case in ('single-read', 'feedback-first', 'latest-wins', 'latest-after-last',
                     'hover-progress', 'drag-progress', 'quick-role-return',
                     'idle-pointer-art', 'volume-pointer-art', 'capture-dirty-art',
                     'measurement-pending-density'):
            result = subprocess.run([str(executable), case], text=True, capture_output=True)
            if result.returncode:
                print(result.stderr)
            result.check_returncode()
            print('PASS: ' + case)
        executable = Path(directory) / 'test-image-cost'
        subprocess.run(['cc', *flags, str(ROOT / 'tools/test_ui_image_cost.c'),
            *[str(main / file) for file in ('echoy_art_image.c', 'echoy_art.c', 'echoy_music.c')],
            '-Wl,--gc-sections', '-lm', '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print('PASS: profiled QOI is byte-identical for 12 expressions; split timing and failure cleanup')
        executable = Path(directory) / 'test-image-ownership'
        subprocess.run(['cc', *flags, str(ROOT / 'tools/test_ui_image_ownership.c'),
            *[str(main / file) for file in ('echoy_model.c', 'echoy_music.c', 'echoy_art_image.c',
                                          'echoy_art.c', 'echoy_strip_image.c')],
            '-Wl,--gc-sections', '-Wl,--wrap=malloc', '-Wl,--wrap=free',
            '-lm', '-o', str(executable)], check=True)
        for case in ('pc-gallery', 'pc-strip', 'device-gallery', 'device-strip',
                     'gallery-allocation-failure', 'strip-allocation-failure',
                     'gallery-submission-failure', 'strip-submission-failure',
                     'pc-gallery-submission-failure', 'device-gallery-decode-failure'):
            result = subprocess.run([str(executable), case], text=True, capture_output=True)
            if result.returncode:
                print(result.stderr)
            result.check_returncode()
            print('PASS: image ownership ' + case)
        executable = Path(directory) / 'test-ui-creation'
        subprocess.run(['cc', *flags, str(ROOT / 'tools/test_ui_creation.c'),
            *[str(main / file) for file in ('echoy_model.c', 'echoy_music.c', 'echoy_art_image.c',
                'echoy_art.c', 'echoy_strip.c', 'echoy_strip_image.c', 'echoy_display.c',
                'echoy_view_cache.c', 'echoy_art_producer.c')], '-Wl,--gc-sections', '-Wl,--wrap=echoy_model_create',
            '-Wl,--wrap=echoy_model_delete', '-pthread', '-lm', '-o', str(executable)], check=True)
        for case in ('none', 'read-only', 'action-only', 'external', 'storage-only',
                     'storage-failure', 'external-storage-failure', 'external-model-failure',
                     'standalone-model-failure', 'external-timer-failure', 'external-close-retry',
                     'external-already-closed', 'standalone-close-retry', 'standalone-already-closed',
                     'legacy-cache', 'external-error', 'external-home-async'):
            result = subprocess.run([str(executable), case], text=True, capture_output=True)
            if result.returncode:
                print(result.stderr)
            result.check_returncode()
            print('PASS: UI creation ' + case)
        executable = Path(directory) / 'test-ui-home-async'
        subprocess.run(['cc', *flags, str(ROOT / 'tools/test_ui_home_async.c'),
            *[str(main / file) for file in ('echoy_model.c', 'echoy_music.c', 'echoy_art_image.c',
                'echoy_art.c', 'echoy_strip.c', 'echoy_strip_image.c', 'echoy_display.c',
                'echoy_view_cache.c', 'echoy_art_producer.c')], '-Wl,--gc-sections',
            '-Wl,--wrap=echoy_model_create', '-Wl,--wrap=echoy_model_delete',
            '-Wl,--wrap=echoy_art_draw', '-pthread', '-lm', '-o', str(executable)], check=True)
        for case in ('role-preview-gate', 'input-during-draw', 'continuous-progress', 'role-replaces-inflight',
                     'static-final-queue-full', 'route-role-visibility', 'startup-fallback',
                     'worker-stop-retry', 'active-owner-close-retry', 'canvas-stop-retry',
                     'release-closed-retry', 'publish-error'):
            result = subprocess.run([str(executable), case], text=True, capture_output=True)
            if result.returncode:
                print(result.stderr)
            result.check_returncode()
            print('PASS: asynchronous home ' + case)


if __name__ == '__main__':
    main()
