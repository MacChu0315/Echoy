#!/usr/bin/env python3
"""Drive three official GSP hosts and the real C desk, then inspect its PCM.

This never builds or replaces a running preview. It snapshots the supplied
binary/bundle, uses private controls and ephemeral loopback endpoints, and only
changes product state through native pointer input or the desk's public controls.
"""
import argparse
from array import array
from contextlib import ExitStack
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import wave

from PIL import Image, ImageChops
from check_home import assert_v4_geometry, strip_events

ROOT = Path(__file__).resolve().parents[1]
BAR_US = 2500000
SAMPLE_RATE = 24000
TILES = 'ABC'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pcm_statistics(samples, begin=0, end=None):
    segment = samples[max(0, begin):end]
    assert segment, f'Empty PCM window: {begin}..{end}'
    peak = max(abs(value) for value in segment)
    return {'frames': len(segment), 'nonzero': sum(value != 0 for value in segment),
            'peak': peak, 'rms': math.sqrt(sum(value * value for value in segment) / len(segment)),
            'clipped': sum(abs(value) >= 32766 for value in segment)}


class DeskCheck:
    def __init__(self, args, output, runtime):
        self.args, self.output, self.runtime = args, output, runtime
        self.processes, self.logs, self.apis = [], [], {}
        self.ready, self.checks, self.windows, self.events = {}, [], {}, []
        self.backend = None
        self.control = runtime / 'control'
        self.control.mkdir(mode=0o700)
        self.wav = output / 'desk-output.wav'
        self.stop_sampler = threading.Event()
        self.sampler = None

    def start(self, command, name):
        stream = (self.output / name).open('wb')
        self.logs.append(stream)
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                 start_new_session=True)
        self.processes.append(child)
        return child

    def alive(self):
        for child in self.processes:
            assert child.poll() is None, f'Owned process {child.pid} exited with {child.returncode}'

    def status(self):
        try:
            return json.loads((self.control / 'status.json').read_text())
        except FileNotFoundError:
            return {}

    def until(self, predicate, message, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.alive()
            state = self.status()
            if predicate(state):
                assert not any(t.get('prepare_failed') for t in state.get('tiles', {}).values()), state
                return state
            time.sleep(.02)
        raise AssertionError(f'{message}; last status={json.dumps(self.status(), ensure_ascii=False)}')

    def frames(self):
        state = self.status()
        if state.get('audio_origin_us') and state.get('late_blocks', 0) == 0:
            return max(0, (time.monotonic_ns() // 1000 - state['audio_origin_us']) * SAMPLE_RATE // 1000000)
        # Retain conservative failure evidence if the audio clock has skipped
        # a late block. Successful timing acceptance requires no skipped block.
        return max(0, (self.wav.stat().st_size - 44) // 2) if self.wav.exists() else 0

    def mark(self, name):
        row = {'name': name, 'monotonic_us': time.monotonic_ns() // 1000,
               'sample_index': self.frames(), 'status': self.status()}
        self.events.append(row)
        (self.output / 'events.json').write_text(json.dumps(self.events, ensure_ascii=False, indent=2) + '\n')
        return row

    def passed(self, message):
        self.checks.append(message)
        print('PASS: ' + message, flush=True)

    def call(self, tile, method, params=None):
        result = self.apis[tile].call(method, params)
        assert 'error' not in result, (tile, method, result)
        return result.get('result')

    def tap(self, tile, name):
        self.mark(f'tap {tile} {name}')
        self.call(tile, 'tap_object', {'name': name})
        self.call(tile, 'wait', {'frames': 3})

    def play_ready(self, tile):
        # v4 intentionally leaves disabled idle text at full opacity. Renderer
        # colour cannot establish permission; use the UI owner's successful
        # enable/show submission plus the authoritative local playback state.
        def enabled(state):
            rows=strip_events((self.output/'desk.log').read_text(),ready=True,tile=tile)
            return rows and rows[-1].get('mode')=='idle' and rows[-1].get('play')==1 and state['tiles'][tile]['phase']==0
        self.until(enabled,tile+' native Play permission did not become ready',timeout=4)

    def visual_ready(self, tile, route):
        path = self.output / f'visible-{route}-{tile}.png'
        def visible(_):
            self.call(tile, 'screenshot', {'path': str(path), 'format': 'png'})
            with Image.open(path) as source:
                image = source.convert('RGB')
                if route == 'removed':
                    return all(low == high for low, high in image.crop((90, 110, 390, 350)).getextrema())
                point = {'home': (48, 196), 'role': (28, 179), 'settings': (24, 150),
                         'status': (240, 190)}[route]
                red, green, blue = image.getpixel(point)
                if route == 'home':
                    return red < 150 and green < 160 and blue < 140
                if route == 'role':
                    return 200 < red < 225 and 210 < green < 230 and 195 < blue < 216
                if route == 'settings':
                    return 210 <= red < 226 and 210 < green < 231 and 198 < blue < 220
                return red > 140 and 70 < green < 150 and blue < 100
        self.until(visible, f'{tile} native {route} controls did not paint', timeout=3)

    def pad(self, tile):
        self.mark(f'pointer pad {tile}')
        self.call(tile, 'feed_pointer', {'x': 240, 'y': 189, 'pressed': True})
        self.call(tile, 'wait', {'frames': 3})
        self.call(tile, 'feed_pointer', {'x': 240, 'y': 189, 'pressed': False})
        self.call(tile, 'wait', {'frames': 3})

    def tap_at(self, tile, x, y):
        self.mark(f'tap {tile} ({x},{y})')
        self.call(tile, 'tap', {'x': x, 'y': y})
        self.call(tile, 'wait', {'frames': 3})

    def drag(self, tile, x, start, end):
        self.mark(f'drag {tile} ({x},{start})->({x},{end})')
        self.call(tile, 'feed_pointer', {'x': x, 'y': start, 'pressed': True})
        self.call(tile, 'wait', {'frames': 2})
        for step in range(1, 8):
            self.call(tile, 'feed_pointer', {'x': x, 'y': round(start + (end-start)*step/7), 'pressed': True})
            self.call(tile, 'wait', {'frames': 2})
        self.call(tile, 'feed_pointer', {'x': x, 'y': end, 'pressed': False})
        self.call(tile, 'wait', {'frames': 3})

    def command(self, value):
        self.mark('control ' + value)
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
            connection.sendto(value.encode('ascii'), str(self.control / 'command.sock'))

    def shot(self, tile, name):
        self.call(tile, 'wait', {'frames': 5})
        path = self.output / f'{name}-{tile}.png'
        self.call(tile, 'screenshot', {'path': str(path), 'format': 'png'})
        with Image.open(path) as image:
            assert image.size == (480, 480)
            assert len(image.convert('RGB').getcolors(480 * 480)) > 20, 'Blank native screen'
        return path

    def stable(self, state):
        return bool(state.get('ready')) and not state.get('pending') and all(
            not t['plan_pending'] and not t['preparing'] for t in state['tiles'].values())

    def export(self, name):
        before = self.status()['export_counter']
        self.command('export')
        self.until(lambda s: s.get('export_counter', 0) > before, 'Session export did not finish')
        data = json.loads((self.control / 'session.json').read_text())
        assert set(data['tiles']) == set(TILES), data
        (self.output / f'{name}.json').write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
        return data

    def sample_status(self):
        previous = None
        with (self.output / 'status-history.jsonl').open('w') as stream:
            while not self.stop_sampler.is_set():
                try:
                    path = self.control / 'status.json'
                    stamp = path.stat().st_mtime_ns
                    if stamp != previous:
                        state = json.loads(path.read_text())
                        row = {'monotonic_us': time.monotonic_ns() // 1000,
                               'sample_index': self.frames(), 'status': state}
                        stream.write(json.dumps(row) + '\n')
                        stream.flush()
                        previous = stamp
                except FileNotFoundError:
                    pass
                self.stop_sampler.wait(.025)

    def setup(self, stack):
        build = self.args.build_dir.resolve()
        source = {'binary': build / 'echoy_desk', 'bundle': build / 'echoy_backend_assets/app.gspb',
                  'api': build / 'echoy_backend_assets/bundle_gsp.api.json'}
        copied, metadata = {}, {}
        for kind, path in source.items():
            before = digest(path)
            copied[kind] = self.runtime / path.name
            shutil.copy2(path, copied[kind])
            assert before == digest(copied[kind]) == digest(path), f'Build changed during snapshot: {path}'
            metadata[kind] = {'source': str(path), 'sha256': before}
        assert_v4_geometry(json.loads(copied['api'].read_text()))
        metadata['command'] = sys.argv
        (self.output / 'runtime-snapshot.json').write_text(json.dumps(metadata, indent=2) + '\n')
        manifest = json.loads((build / 'echoy_backend-Release.json').read_text())
        sys.path.insert(0, str(Path(manifest['component_root']) / 'tools/sim_bridge'))
        from rpc_client import connect
        hosts = {}
        for tile in TILES:
            ready_file = self.runtime / f'ready-{tile}.json'
            with socket.socket() as backend_socket:
                backend_socket.bind(('127.0.0.1', 0))
                backend_port = backend_socket.getsockname()[1]
            hosts[tile] = self.start([str(self.args.sim), '--bundle', str(copied['bundle']),
                '--api-json', str(copied['api']), '--backend-listen',f'tcp://127.0.0.1:{backend_port}', '--backend-required',
                '--api-listen', 'tcp://127.0.0.1:0', '--headless', '--fps', '1000', '--frames', '0',
                '--fail-on-error', '--ready-file', str(ready_file)], f'host-{tile}.log')
            self.until(lambda _: ready_file.exists(), f'Host {tile} readiness')
            info = json.loads(ready_file.read_text())
            assert info['pid'] == hosts[tile].pid
            assert info['api'].startswith('tcp://127.0.0.1:') and info['backend'].startswith('tcp://127.0.0.1:')
            self.ready[tile] = info
            self.apis[tile] = stack.enter_context(connect(port=int(info['api'].rsplit(':', 1)[1])))
        (self.output / 'ready.json').write_text(json.dumps(self.ready, indent=2) + '\n')
        command = [str(copied['binary'])]
        for tile in TILES:
            command += [f'--endpoint-{tile}', self.ready[tile]['backend']]
        command += ['--control-dir', str(self.control), '--silent', '--capture', str(self.wav),
                    '--duration-ms', str(self.args.duration_ms)]
        self.backend_started = time.monotonic()
        self.backend = self.start(command, 'desk.log')
        self.sampler = threading.Thread(target=self.sample_status, daemon=True)
        self.sampler.start()
        self.until(lambda s: s.get('ready') is True, 'Three native UI owners did not become ready')
        self.until(self.stable, 'Initial preparation did not settle')
        for tile in TILES:
            capabilities = self.call(tile, 'capabilities')
            assert capabilities['runtime_version'] == '1.5.1', capabilities
            assert capabilities['bridge_pointer_version'] == 1 and capabilities['bridge_media_version'] == 1
            (self.output / f'capabilities-{tile}.json').write_text(json.dumps(capabilities, indent=2) + '\n')

    def at_bar_start(self):
        origin = self.status()['tiles']['B']['clock_origin_us']
        now = time.monotonic_ns() // 1000
        at = origin + ((now - origin) // BAR_US + 1) * BAR_US + 100000
        self.until(lambda _: time.monotonic_ns() // 1000 >= at, 'Next logical bar did not arrive', timeout=3)

    def change_at_boundary(self, name, action, expect):
        before = self.export(name + '-before')
        self.at_bar_start()
        start = self.mark(name + '-old-audio-start')['sample_index']
        revision = self.status()['revision']
        action()
        pending = self.until(lambda s: s.get('revision', 0) > revision and
            any(t['plan_pending'] for t in s['tiles'].values()), name + ' did not create pending work', timeout=3)
        assert pending['tiles']['B']['loop'] and pending['tiles']['C']['loop'], name + ' removed playback permission'
        staged = self.until(lambda s: s['tiles']['B']['apply_at_us'] > 0, name + ' has no staged boundary', timeout=3)
        boundary = staged['tiles']['B']['apply_at_us']
        assert (boundary - staged['tiles']['B']['clock_origin_us']) % BAR_US in (0, 1, BAR_US-1)
        assert staged['status_at_us'] < boundary, name + ' boundary was not in the future'
        during = self.export(name + '-pending')
        assert during['tiles']['B']['current'] == before['tiles']['B']['current'], name + ' replaced B before the bar'
        assert during['tiles']['C']['current'] == before['tiles']['C']['current'], name + ' replaced C before the bar'
        self.until(self.stable, name + ' did not commit at its boundary', timeout=5)
        end = self.mark(name + '-committed')['sample_index']
        self.windows[name + '-continues'] = [start, end]
        after = self.export(name + '-after')
        expect(after['tiles']['B']['current'])
        assert after['tiles']['B']['current'] != before['tiles']['B']['current']
        assert self.status()['tiles']['C']['source'] == 'B'
        return before, after

    def exercise(self):
        initial = self.status()
        assert initial['tiles']['A']['present'] and not initial['tiles']['B']['present'] and not initial['tiles']['C']['present']
        assert all(t['seed'] == 0 and t['phase'] == 0 for t in initial['tiles'].values())
        self.shot('A', 'initial')
        start = self.mark('ordinary-pad-before')['sample_index']
        self.pad('A')
        time.sleep(.6)
        self.windows['ordinary-pad'] = [start, self.mark('ordinary-pad-after')['sample_index']]
        assert self.status()['tiles']['A']['phase'] == 0 and self.status()['tiles']['A']['seed'] == 0
        live = array('h', self.wav.read_bytes()[44:])
        if sys.byteorder != 'little':
            live.byteswap()
        pad_pcm = pcm_statistics(live, *self.windows['ordinary-pad'])
        assert pad_pcm['nonzero'] > 100 and pad_pcm['peak'] > 100, pad_pcm
        self.passed('Native pointer pad tap produces nonzero PCM and stays outside recording')

        start = self.mark('example-before')['sample_index']
        self.command('example A')
        preview = self.until(lambda s: s.get('tiles', {}).get('A', {}).get('phase') == 4, 'Example did not enter one-shot preview')
        assert not preview['tiles']['A']['loop']
        self.until(lambda s: s.get('tiles', {}).get('A', {}).get('phase') == 0 and self.stable(s), 'Example did not stop after one reply', timeout=6)
        self.windows['example-once'] = [start, self.mark('example-ended')['sample_index']]
        assert self.status()['tiles']['A']['seed'] == 5
        self.passed('Example A commits a real seed, plays one reply and stops without loop permission')

        self.command('present B 1'); self.command('present C 1'); self.command('edges A B B C')
        linked = self.until(lambda s: self.stable(s) and s['tiles']['B']['source'] == 'A' and
            s['tiles']['C']['source'] == 'B', 'Three-device chain did not commit')
        assert linked['edges'] == [{'left': 'A', 'right': 'B'}, {'left': 'B', 'right': 'C'}]
        assert all(t['phase'] == 0 and not t['loop'] and t['events'] > 2 for t in linked['tiles'].values())
        first_export = self.export('linked-silent')
        for tile in TILES:
            plan = first_export['tiles'][tile]['current']
            assert plan['rootSeed'] == first_export['tiles']['A']['current']['rootSeed']
            assert len(plan['full']) > len(plan['rootSeed'])
            self.shot(tile, 'linked-silent')
        self.passed('A→B→C receives full linked plans and a common root seed; new tiles remain silent')

        for tile in TILES:
            self.play_ready(tile)
            self.tap(tile, 'transport')
            self.until(lambda s, tile=tile: s.get('tiles', {}).get(tile, {}).get('loop') and
                s['tiles'][tile]['phase'] == 5, tile + ' transport did not enable its own playback')
        playing = self.status()['tiles']
        origins = {t['clock_origin_us'] for t in playing.values()}
        assert len(origins) == 1 and next(iter(origins)) > 0, playing
        origin = next(iter(origins))
        for tile in TILES:
            assert playing[tile]['clock'] == 'A'
            remainder = (playing[tile]['start_us'] - origin) % BAR_US
            assert remainder in (0, 1, BAR_US-1), (tile, playing[tile])
        self.until(lambda _: time.monotonic_ns() // 1000 > max(t['start_us'] for t in playing.values()) + 200000,
                   'Scheduled shared-beat playback did not arrive', timeout=3)
        self.tap('A', 'strip_action')
        self.until(lambda s: not s['tiles']['A']['loop'] and s['tiles']['A']['phase'] == 0,
                   'A did not pause independently')
        assert all(self.status()['tiles'][tile]['loop'] for tile in 'BC')
        self.passed('Each native Play grants local permission at the shared bar; pausing A leaves B/C playing')

        self.tap('B', 'role'); self.visual_ready('B', 'role')
        def role_is_pulse(plan):
            assert plan['role'] == 'pulse', plan
        before, changed = self.change_at_boundary('B-role', lambda: self.tap('B', 'next_role'), role_is_pulse)
        assert before['tiles']['C']['current'] != changed['tiles']['C']['current'], 'C ignored B’s changed tonal plan'
        def phrase_is_three(plan):
            assert plan['phrase'] == 3, plan
        self.change_at_boundary('B-phrase', lambda: self.tap('B', 'phrase_3'), phrase_is_three)
        self.tap('B', 'role_back'); self.visual_ready('B', 'home')
        def density_is_full(plan):
            assert abs(plan['density'] - 1) < 1e-12, plan
        self.change_at_boundary('B-density', lambda: self.drag('B', 432, 256, 116), density_is_full)
        self.passed('B role/phrase/richness change at bar boundaries; old B/C plans and playback continue during preparation')

        attached = self.export('before-detach')
        self.command('edges')
        self.until(lambda s: self.stable(s) and not s['edges'] and s['tiles']['B']['source'] is None and
            s['tiles']['C']['source'] is None, 'Detach did not settle', timeout=5)
        detached = self.export('after-detach')
        for tile in 'BC':
            old = {k: v for k, v in attached['tiles'][tile]['current'].items() if k not in ('source', 'detached')}
            new = {k: v for k, v in detached['tiles'][tile]['current'].items() if k not in ('source', 'detached')}
            assert new == old, tile + ' lost its exact phrase on detach'
            assert detached['tiles'][tile]['current']['source'] is None and detached['tiles'][tile]['current']['detached']
            assert detached['tiles'][tile]['loop'] and self.status()['tiles'][tile]['phase'] == 5
        self.passed('Detaching preserves exact B/C musical events and independent continuous-play permission')

        self.command('condition B fault')
        self.until(lambda s: s['tiles']['B']['condition'] == 'fault' and not s['tiles']['B']['loop'], 'Fault did not mute B')
        self.visual_ready('B', 'status')
        self.shot('B', 'fault')
        self.tap('B', 'recover')
        self.until(lambda s: s['tiles']['B']['condition'] == 'normal' and not s['tiles']['B']['loop'] and
            s['tiles']['B']['phase'] == 0, 'Native recover did not stay silent')
        self.visual_ready('B', 'home')
        assert self.status()['tiles']['C']['loop']
        self.command('present C 0')
        self.until(lambda s: not s['tiles']['C']['present'] and not s['tiles']['C']['loop'], 'Desktop put-away did not remove C')
        self.visual_ready('C', 'removed')
        self.command('present C 1')
        self.until(lambda s: self.stable(s) and s['tiles']['C']['present'] and
            not s['tiles']['C']['loop'] and s['tiles']['C']['phase'] == 0, 'Restored C autoplayed or did not settle')
        self.visual_ready('C', 'home')
        self.passed('Fault recovery and desktop put-away/restore retain data and never autoplay')

        exported = self.export('session')
        assert all(t['current']['full'] for t in exported['tiles'].values())
        assert exported['tiles']['B']['current'] == detached['tiles']['B']['current']
        assert exported['tiles']['C']['current']['rootSeed'] == detached['tiles']['C']['current']['rootSeed']
        for tile in TILES:
            self.shot(tile, 'final')
        self.passed('Desktop Export contains all three actual full plans and preserved root seeds')

        self.until(lambda s: all(not t['loop'] and t['phase'] == 0 for t in s['tiles'].values()), 'Final playback did not stop')
        time.sleep(.25)
        start = self.mark('paused-tail-start')['sample_index'] + 4096
        time.sleep(.55)
        end = self.mark('paused-tail-end')['sample_index']
        assert end > start
        self.windows['paused-silence'] = [start, end]

        # ArrangeApp.lang is shared by the complete desk in the frozen HTML.
        # Keep this before natural teardown, after the existing interaction and
        # paused-audio checks, so the regression uses all three real UI owners.
        self.tap('A', 'settings')
        self.visual_ready('A', 'settings')
        self.shot('A', 'language-before')
        previous = (self.output / 'desk.log').read_text().count('ECHOY_LOCALE en\n')
        self.tap('A', 'language_en')
        self.until(lambda s: all(t.get('english') is True for t in s['tiles'].values()) and
            (self.output / 'desk.log').read_text().count('ECHOY_LOCALE en\n') >= previous + 3,
            'A language selection did not update all three native UI owners')
        originals = {}
        for tile in TILES:
            path = self.output / ('language-before-A.png' if tile == 'A' else f'final-{tile}.png')
            with Image.open(path) as image:
                originals[tile] = image.convert('RGB').crop((108, 30, 382, 75) if tile == 'A' else (30, 20, 230, 81))
        def localized_frames(_):
            changed = True
            for tile in TILES:
                path = self.output / f'english-{tile}.png'
                self.call(tile, 'screenshot', {'path': str(path), 'format': 'png'})
                with Image.open(path) as image:
                    region = image.convert('RGB').crop((108, 30, 382, 75) if tile == 'A' else (30, 20, 230, 81))
                    changed &= ImageChops.difference(originals[tile], region).getbbox() is not None
            return changed
        self.until(localized_frames, 'English text did not paint on all three native screens', timeout=3)
        assert (self.output / 'desk.log').read_text().count('ECHOY_LOCALE en\n') == previous + 3
        assert all(not t['loop'] and t['phase'] == 0 for t in self.status()['tiles'].values())
        self.passed('A native EN selection updates the shared desktop language and all three rendered screens without playback')

        musical_before={tile:{key:self.status()['tiles'][tile][key] for key in ('seed','phase','loop','events','clock_origin_us')} for tile in TILES}
        for reduce in (True,False):
            self.command('motion '+str(int(reduce)))
            state=self.until(lambda s:all(t.get('reduced_motion') is reduce for t in s['tiles'].values()),'Reduced-motion preference did not reach all three C views')
            for tile in TILES:
                assert {key:state['tiles'][tile][key] for key in musical_before[tile]}==musical_before[tile]
        self.passed('Shared reduced-motion preference reaches all three native views without changing music or playback permission')

    def finish(self):
        # Let the backend reach its configured duration and run its normal
        # teardown, rather than treating signal termination as a pass.
        deadline = self.backend_started + self.args.duration_ms / 1000 + 8
        while self.backend.poll() is None and time.monotonic() < deadline:
            time.sleep(.1)
        assert self.backend.poll() == 0, f'Natural desk cleanup failed: {self.backend.poll()}'
        self.stop_sampler.set()
        if self.sampler:
            self.sampler.join(timeout=2)
        for tile in TILES:
            self.call(tile, 'quit')
        for child in self.processes[:-1]:
            assert child.wait(timeout=6) == 0
        log = (self.output / 'desk.log').read_text()
        assert 'ECHOY_DESK_CLOSED error=0' in log
        assert re.search(r'ECHOY_DESK_CLOSED error=0 samples=\d+ late_blocks=0\b', log), 'Audio scheduler dropped a late block'
        assert all(f'ECHOY_DESK_UI tile={tile} closed=1' in log for tile in TILES)
        for bad in ['ECHOY_UI_ERROR', 'ECHOY_DESK_UI_ERROR', 'ECHOY_AUDIO_PREPARE_FAILED', 'ECHOY_DESK_COMMAND_REJECTED',
                    'No free canvas slot', 'Failed to decode']:
            assert bad not in log, bad
        for tile in TILES:
            host_log = (self.output / f'host-{tile}.log').read_text()
            assert 'session complete' in host_log, host_log[-2000:]
            assert not re.search(r'\bERROR\b|^E \(', host_log, re.MULTILINE), host_log[-2000:]
        self.passed('All three native UI owners and official hosts exit naturally without UI or cleanup errors')

        with wave.open(str(self.wav), 'rb') as recording:
            assert recording.getnchannels() == 1 and recording.getsampwidth() == 2 and recording.getframerate() == SAMPLE_RATE
            samples = array('h', recording.readframes(recording.getnframes()))
        if sys.byteorder != 'little':
            samples.byteswap()
        stats = {'all': pcm_statistics(samples)}
        for name, (begin, end) in self.windows.items():
            stats[name] = pcm_statistics(samples, begin, end)
        assert stats['all']['nonzero'] > SAMPLE_RATE and stats['all']['clipped'] == 0, stats
        for name in ['ordinary-pad', 'example-once', 'B-role-continues', 'B-phrase-continues', 'B-density-continues']:
            assert stats[name]['nonzero'] > 100 and stats[name]['peak'] > 100, (name, stats[name])
        assert stats['paused-silence']['nonzero'] == 0, stats['paused-silence']
        history = [json.loads(line) for line in (self.output / 'status-history.jsonl').read_text().splitlines()]
        assert not any(t['phase'] in (1, 2) for row in history for t in row['status']['tiles'].values()), 'Non-record input started capture'
        (self.output / 'audio-metrics.json').write_text(json.dumps({'sample_rate': SAMPLE_RATE,
            'frames': len(samples), 'windows': self.windows, 'metrics': stats,
            'silent_adapter': True, 'scope': 'PCM produced by the real C desk output path; no physical speaker/wireless acceptance'}, indent=2) + '\n')
        self.passed('Actual mixed PCM is nonzero for taps/replies/continued playback, unclipped, and exactly silent after all pauses')
        (self.output / 'checks.json').write_text(json.dumps({'passed': self.checks,
            'scope': 'Three official GSP hosts with native C input, linked music, logical clocks and captured PCM; no physical device acceptance'}, ensure_ascii=False, indent=2) + '\n')

    def close(self):
        self.stop_sampler.set()
        if self.sampler:
            self.sampler.join(timeout=2)
        for name in ['status.json', 'session.json']:
            if (self.control / name).exists():
                shutil.copy2(self.control / name, self.output / ('last-' + name))
        for child in reversed(self.processes):
            if child.poll() is None:
                try:
                    os.killpg(child.pid, signal.SIGTERM)
                except ProcessLookupError:
                    continue
                try:
                    child.wait(timeout=6)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
        for stream in self.logs:
            stream.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'projects/echoy/build-sim-v4')
    parser.add_argument('--sim', type=Path, default=ROOT.parent / '.toolchains/gsp-sim-1.5.1/sim')
    parser.add_argument('--output', type=Path, default=ROOT / 'output/v4-desk-native-check')
    parser.add_argument('--duration-ms', type=int, default=60000)
    args = parser.parse_args()
    assert 45000 <= args.duration_ms <= 180000, 'Use a finite duration that covers the complete input scenario'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    assert not any(output.iterdir()), 'Choose an empty output directory to preserve earlier evidence'
    with tempfile.TemporaryDirectory(prefix='echoy-desk-check-') as temporary:
        check = DeskCheck(args, output, Path(temporary))
        try:
            with ExitStack() as stack:
                try:
                    check.setup(stack)
                    check.exercise()
                    check.finish()
                except BaseException:
                    for tile in TILES:
                        if tile in check.apis and check.backend and check.backend.poll() is None:
                            try:
                                check.shot(tile, 'failure')
                            except Exception:
                                pass
                    raise
        except BaseException as error:
            (output / 'failure.json').write_text(json.dumps({'error': str(error), 'state': check.status(),
                'passed': check.checks, 'windows': check.windows}, ensure_ascii=False, indent=2) + '\n')
            raise
        finally:
            check.close()


if __name__ == '__main__':
    main()
