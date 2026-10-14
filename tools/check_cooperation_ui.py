#!/usr/bin/env python3
"""Check cooperation captions and real native pointer flows without hardware writes.

The C checks call the shipped renderer with controlled snapshots; the optional
desk checks use three official GSP hosts and the shared C controller/audio owner.
No build directory or running preview is changed by this checker.
"""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import subprocess
import tempfile
import time

from fontTools.ttLib import TTFont
from PIL import Image, ImageChops, ImageFont

from check_desk import DeskCheck, ROOT, TILES
from check_home import compact_message, record_hint_visible, strip_events

SOLO, CONNECTED, WAITING, DEFERRED, FOLLOWING, RECOVERING = range(6)


def check_captions(build, output, runtime):
    main = ROOT / 'projects/echoy/main'
    component = ROOT / 'projects/echoy/managed_components/espressif__esp-gsp'
    executable = runtime / 'cooperation-ui-test'
    command = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
               '-fdata-sections', '-I' + str(main),
               '-I' + str(build / 'echoy_backend_assets'),
               '-I' + str(component / 'tools/sim_bridge/include'),
               '-I' + str(component / 'include'), str(ROOT / 'tools/test_cooperation_ui.c'),
               str(main / 'echoy_model.c'), str(main / 'echoy_music.c'),
               '-Wl,--gc-sections', '-lm', '-o', str(executable)]
    result = subprocess.run(command, text=True, capture_output=True)
    (output / 'caption-build.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    result = subprocess.run([str(executable)], text=True, capture_output=True)
    (output / 'caption-test.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    scene = json.loads((ROOT / 'projects/echoy/ui/main.json').read_text())
    objects = {o['name']: o for o in scene['objects']}
    font_packs = {(o.get('font', scene['font']), o.get('font_size', scene['default_font_size']))
                  for o in scene['objects'] if o['type'] == 'label'}
    assert len(font_packs) <= 32, font_packs
    checked = []
    for row in result.stdout.splitlines():
        english, caption = row.split('\t', 1)
        obj = objects['status_en' if english == '1' else 'status']
        font_path = ROOT / 'projects/echoy/ui' / obj.get('font', scene['font'])
        font = ImageFont.truetype(str(font_path), obj['font_size'])
        width = font.getlength(caption)
        assert width <= obj['w'], (caption, width, obj['w'])
        assert set(caption) <= set(obj['font_charset']), ('Missing runtime charset', caption)
        with TTFont(font_path) as source_font:
            cmap = source_font.getBestCmap()
            assert all(ord(ch) in cmap for ch in caption), ('Missing source glyph', caption)
        checked.append({'language': 'en' if english == '1' else 'zh',
                        'caption': caption, 'width': width, 'available_width': obj['w']})
    (output / 'captions.json').write_text(json.dumps({'font_packs': len(font_packs),
        'checked': checked}, ensure_ascii=False, indent=2) + '\n')
    print(f'PASS: {len(checked)} real C status/visibility assertions; '
          f'all captions fit, all glyphs exist, {len(font_packs)} font packs', flush=True)


class CooperationCheck(DeskCheck):
    def cooperation(self, tile, expected, phase=None, stable=False):
        return self.until(lambda s: s['tiles'][tile]['cooperation'] == expected and
                          (phase is None or s['tiles'][tile]['phase'] == phase) and
                          (not stable or self.stable(s)),
                          f'{tile} did not reach cooperation={expected}, phase={phase}')

    def caption_shot(self, tile, name):
        def settled(_):
            rows=strip_events((self.output/'desk.log').read_text(),tile=tile)
            return rows and time.monotonic_ns()//1000 >= rows[-1]['at_us'] + rows[-1]['duration_ms']*1000 + 40000
        self.until(settled,'Native strip transition did not settle')
        path = self.shot(tile, name)
        with Image.open(path) as source:
            image = source.convert('RGB')
            assert compact_message(image, image.getpixel((100, 80))), 'Status line is blank'
            assert not record_hint_visible(image), 'Record reminder hides cooperation state'
            # A status line may not paint below its established 23 px box.
            paper = image.getpixel((100, 80))
            assert all(pixel == paper for pixel in image.crop((24, 365, 456, 375)).getdata()), \
                'Status text overflowed its existing area'
        return path

    def detach(self):
        self.command('edges')
        self.cooperation('B', SOLO, stable=True)

    def reply_started(self):
        state = self.cooperation('B', DEFERRED, phase=4)
        self.until(lambda s: s['tiles']['B']['phase'] == 4 and
                   s['tiles']['B']['cooperation'] == DEFERRED and
                   time.monotonic_ns() // 1000 >= state['tiles']['B']['start_us'] + 100000,
                   'Deferred one-shot reply did not start')

    def record_join(self, join_phase, suffix):
        self.detach()
        self.record_ready('B')
        # The strip is Record-only when empty, or the same left-hand hit area
        # when a retained phrase exists. Pointer coordinates work for both.
        self.tap_at('B', 170, 420)
        self.until(lambda s: s['tiles']['B']['phase'] == join_phase,
                   f'B did not enter recording phase {join_phase}')
        self.command('edges A B')
        self.cooperation('B', DEFERRED, phase=join_phase)
        self.caption_shot('B', 'join-' + suffix)
        if join_phase == 1:
            self.until(lambda s: s['tiles']['B']['phase'] == 2, 'Deferred count-in was cancelled')
        self.pad('B')
        self.cooperation('B', DEFERRED, phase=2)
        self.caption_shot('B', 'recording-' + suffix)
        self.reply_started()
        self.caption_shot('B', 'reply-' + suffix)
        ended = self.cooperation('B', WAITING, phase=0, stable=True)
        assert not ended['tiles']['B']['loop'], ended
        self.caption_shot('B', 'joined-' + suffix)

    def exercise(self):
        self.command('present B 1')
        self.until(lambda s: s['tiles']['B']['present'] and self.stable(s), 'B did not appear')
        self.visual_ready('B', 'home')
        solo = self.shot('B', 'solo')
        self.command('edges A B')
        self.cooperation('A', CONNECTED, stable=True)
        self.cooperation('B', WAITING, phase=0)
        self.caption_shot('A', 'connected')
        waiting = self.caption_shot('B', 'waiting')
        with Image.open(solo) as before, Image.open(waiting) as after:
            assert ImageChops.difference(before.crop((24, 338, 456, 365)),
                                         after.crop((24, 338, 456, 365))).getbbox(), \
                'Empty follower still displays the solo recording hint'
        self.tap_at('B', 170, 420)
        state = self.cooperation('B', WAITING, phase=0)
        assert not state['tiles']['B']['seed'] and not state['tiles']['B']['loop']
        self.passed('Empty linked follower shows waiting and its disabled Record stays disabled')

        self.record_join(1, 'count-in-zh')
        self.record_join(2, 'active-recording-zh')
        self.passed('Joining during native count-in and recording preserves capture/reply and joins afterward')

        self.detach()
        self.record_ready('B')
        self.tap_at('B', 170, 420)
        self.until(lambda s: s['tiles']['B']['phase'] == 1, 'Cancel setup did not count in')
        self.command('edges A B')
        self.cooperation('B', DEFERRED, phase=1)
        self.tap('B', 'strip_action')
        self.cooperation('B', WAITING, phase=0, stable=True)
        self.caption_shot('B', 'cancel-keeps-link')
        self.passed('Native Cancel ends the deferred operation and retains visible connection feedback')

        self.detach()
        self.record_ready('B')
        self.tap_at('B', 170, 420)
        self.until(lambda s: s['tiles']['B']['phase'] == 1, 'Detach setup did not count in')
        self.command('edges A B')
        self.cooperation('B', DEFERRED, phase=1)
        self.detach()
        assert self.status()['tiles']['B']['phase'] in (1, 2)
        self.caption_shot('B', 'detach-keeps-recording')
        self.tap('B', 'strip_action')
        self.cooperation('B', SOLO, phase=0, stable=True)
        self.passed('Detaching a deferred join preserves the native capture and removes its join promise')

        self.command('example A')
        self.until(lambda s: s['tiles']['A']['phase'] == 4, 'Leader example did not reply')
        self.until(lambda s: s['tiles']['A']['phase'] == 0 and self.stable(s), 'Leader reply did not finish')
        self.command('edges A B')
        self.cooperation('B', FOLLOWING, phase=0, stable=True)
        play_only=self.caption_shot('B', 'following-idle')
        with Image.open(play_only) as image:
            ink=image.convert('RGB').crop((145,396,335,443))
            assert not any(r>g+20 and r>b+20 for r,g,b in ink.getdata()), 'Follower still shows the orange Record glyph'
            assert image.getpixel((240,401))[:3] == image.getpixel((200,401))[:3], 'Follower still shows the divider'
        self.play_ready('B')
        self.tap('B', 'transport')
        state = self.cooperation('B', FOLLOWING, phase=5)
        self.until(lambda _: time.monotonic_ns() // 1000 >= state['tiles']['B']['start_us'],
                   'Follower playback did not start')
        self.caption_shot('B', 'following-playing')
        self.tap('B', 'strip_action')
        self.cooperation('B', FOLLOWING, phase=0, stable=True)
        self.passed('Following text remains visible while idle and during explicitly requested playback')

        # Both halves of the retained 244x84 area belong to centered Play.
        for x in (140, 338):
            self.play_ready('B')
            before = time.monotonic_ns() // 1000
            self.tap_at('B', x, 420)
            state = self.cooperation('B', FOLLOWING, phase=5)
            assert state['tiles']['B']['start_us'] - before < 300000
            assert not state['tiles']['A']['loop'], 'Clock establishment granted peer playback'
            self.tap('B', 'strip_action')
            self.cooperation('B', FOLLOWING, phase=0, stable=True)
        self.passed('Centered follower Play accepts both sides of its full area and starts a paused group promptly')

        self.play_ready('B')
        self.tap_at('B', 240, 420)
        self.cooperation('B', FOLLOWING, phase=5)
        retained = self.export('before-physical-release')['tiles']['B']['current']
        self.command('edges')
        state = self.cooperation('B', SOLO, phase=5)
        assert state['tiles']['B']['source'] is None and state['tiles']['B']['clock'] == 'B'
        assert state['tiles']['B']['loop'], 'Release muted existing playback'
        self.until(lambda s: self.stable(s), 'Release did not settle')
        after = self.export('after-physical-release')['tiles']['B']['current']
        assert after['full'] == retained['full'] and after['rootSeed'] == retained['rootSeed']
        self.tap('B', 'strip_action')
        self.cooperation('B', SOLO, phase=0, stable=True)
        self.play_ready('B')
        before = time.monotonic_ns() // 1000
        self.tap('B', 'transport')
        state = self.cooperation('B', SOLO, phase=5)
        assert state['tiles']['B']['start_us'] - before < 300000
        self.shot('B', 'independent-playing')
        self.tap('B', 'strip_action')
        self.cooperation('B', SOLO, phase=0, stable=True)
        self.passed('Native release restores independent controls, retains the phrase/play state and permits prompt replay')

        self.command('edges A B')
        self.cooperation('B', FOLLOWING, phase=0, stable=True)
        self.tap('A', 'settings')
        self.visual_ready('A', 'settings')
        self.tap('A', 'language_en')
        self.until(lambda s: all(t['english'] for t in s['tiles'].values()), 'English did not apply')
        self.tap('A', 'settings_back')
        self.caption_shot('B', 'following-en')
        self.detach()
        self.record_ready('B')
        self.tap_at('B', 170, 420)
        self.until(lambda s: s['tiles']['B']['phase'] == 1, 'English count-in did not start')
        self.command('edges A B')
        self.cooperation('B', DEFERRED, phase=1)
        self.caption_shot('B', 'deferred-count-in-en')
        self.until(lambda s: s['tiles']['B']['phase'] == 2, 'English recording did not start')
        self.pad('B')
        self.caption_shot('B', 'deferred-recording-en')
        self.reply_started()
        self.caption_shot('B', 'deferred-reply-en')
        state = self.cooperation('B', FOLLOWING, phase=0, stable=True)
        assert not state['tiles']['B']['loop']
        self.caption_shot('B', 'following-after-reply-en')
        self.passed('English deferred progress and one-shot reply use the unchanged status area')
        self.export('cooperation-final')
        log = (self.output / 'desk.log').read_text()
        assert 'ECHOY_UI_ERROR' not in log and 'ECHOY_DESK_ERROR' not in log, log[-2000:]
        (self.output / 'checks.json').write_text(json.dumps({'passed': self.checks,
            'scope': 'Native shared C/GSP captions and pointer flows; physical wireless/audio acceptance excluded'},
            ensure_ascii=False, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'projects/echoy/build-sim-reminder-popup')
    parser.add_argument('--sim', type=Path, default=ROOT.parent / '.toolchains/gsp-sim-1.5.1/sim')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration-ms', type=int, default=90000)
    parser.add_argument('--unit-only', action='store_true')
    args = parser.parse_args()
    assert 45000 <= args.duration_ms <= 180000
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    assert not any(output.iterdir()), 'Choose an empty output directory to preserve earlier evidence'
    with tempfile.TemporaryDirectory(prefix='echoy-cooperation-check-') as temporary:
        runtime = Path(temporary)
        check_captions(args.build_dir.resolve(), output, runtime)
        if args.unit_only:
            return
        check = CooperationCheck(args, output, runtime)
        try:
            with ExitStack() as stack:
                try:
                    check.setup(stack)
                    check.exercise()
                except BaseException:
                    for tile in TILES:
                        if tile in check.apis and check.backend and check.backend.poll() is None:
                            try:
                                check.shot(tile, 'failure')
                            except Exception:
                                pass
                    raise
        except BaseException as error:
            (output / 'failure.json').write_text(json.dumps({'error': str(error),
                'state': check.status(), 'passed': check.checks}, ensure_ascii=False, indent=2) + '\n')
            raise
        finally:
            check.close()


if __name__ == '__main__':
    main()
