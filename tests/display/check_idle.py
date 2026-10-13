"""Verify manual brightness survives elapsed idle/playback and native touch."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from check_home import snapshot_backend


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = snapshot_backend(args.build_dir, out)
    sys.path.insert(0, str(Path(manifest['component_root']) / 'tools/sim_bridge'))
    from rpc_client import connect
    with socket.socket() as s, socket.socket() as b:
        s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
        b.bind(('127.0.0.1', 0)); backend_port = b.getsockname()[1]
    ready = out / 'ready.json'
    ready.unlink(missing_ok=True)
    processes = []
    with (out / 'host.log').open('w') as hf, (out / 'backend.log').open('w') as bf:
        def log():
            return (out / 'backend.log').read_text()

        def until(predicate, timeout=8):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                if predicate():
                    return
                assert all(p.poll() is None for p in processes), log()
                time.sleep(.05)
            raise AssertionError(log())

        try:
            host = subprocess.Popen([
                str(ROOT.parent / '.toolchains/gsp-sim-1.5.1/sim'), '--bundle', manifest['bundle'],
                '--api-json', manifest['api'], '--backend-listen', f'tcp://127.0.0.1:{backend_port}',
                '--backend-required', '--api-listen', f'tcp://127.0.0.1:{port}', '--headless',
                '--fps', '1000', '--frames', '0', '--ready-file', str(ready)], stdout=hf, stderr=subprocess.STDOUT)
            processes.append(host)
            until(ready.exists)
            info = json.loads(ready.read_text())
            assert info['pid'] == host.pid
            backend = subprocess.Popen([manifest['executable'], '--endpoint', info['backend'],
                                        '--duration-ms', '138000'], env={**os.environ, 'ECHOY_SIM_SEED': '44a0'},
                                       stdout=bf, stderr=subprocess.STDOUT)
            processes.append(backend)
            until(lambda: 'ECHOY_STRIP_READY tile=A mode=idle record=1 play=1' in log())
            with connect(port=port) as api:
                def call(method, params=None):
                    result = api.call(method, params)
                    assert 'error' not in result, result
                    return result.get('result')

                def shot(name):
                    call('wait', {'frames': 5})
                    path = out / (name + '.png')
                    call('screenshot', {'path': str(path), 'format': 'png'})
                    return Image.open(path).convert('RGB').getpixel((240, 80))

                def tap(name):
                    call('tap_object', {'name': name})
                    time.sleep(.15)
                    call('wait', {'frames': 5})

                def route(name):
                    until(lambda: re.findall(r'ECHOY_ROUTE route=(\w+)', log())[-1] == name)

                def brightness(percent):
                    rows = re.findall(r'ECHOY_DISPLAY brightness=(\d+) effective=(\d+) ok=(\d+)', log())
                    return bool(rows) and rows[-1] == (str(percent), str(percent), '1')

                def set_level(x, percent):
                    for pressed in (True, False):
                        call('feed_pointer', {'x': x, 'y': 193, 'pressed': pressed})
                        call('wait', {'frames': 3})
                    until(lambda: brightness(percent))

                time.sleep(.15)
                call('wait', {'frames': 5})
                bright = shot('home-default-100')
                assert brightness(100), 'Default brightness must be 100%'
                tap('settings'); route('settings_page')
                tap('settings_display'); route('display_page')
                set_level(240, 60)
                shot('display-manual-60')
                tap('display_back'); route('settings_page')
                tap('settings_back'); route('home')
                manual = shot('home-manual-60')
                assert sum(manual) < sum(bright) * .7, (bright, manual)
                manual_log_at = len(log())
                began = time.monotonic()
                until(lambda: time.monotonic() - began > 61, 64)
                idle_seconds = time.monotonic() - began
                assert shot('idle-after-minute') == manual
                assert brightness(60), 'Idle changed the manual brightness'
                before = log().count('ECHOY_STATE phase=')
                for pressed in (True, False):
                    call('feed_pointer', {'x': 240, 'y': 80, 'pressed': pressed})
                    call('wait', {'frames': 3})
                assert shot('touch-keeps-60') == manual
                assert brightness(60), 'Neutral touch changed the manual brightness'
                assert log().count('ECHOY_STATE phase=') == before, 'Neutral touch changed music state'
                tap('transport')
                until(lambda: 'ECHOY_STATE phase=5' in log())
                began = time.monotonic()
                until(lambda: time.monotonic() - began > 61, 64)
                playing_seconds = time.monotonic() - began
                assert shot('playing-after-minute') == manual
                assert brightness(60), 'Playback changed the manual brightness'
                assert 'ECHOY_DISPLAY' not in log()[manual_log_at:], 'Brightness changed without slider input'
                tap('strip_action')
                until(lambda: re.findall(r'ECHOY_STATE phase=(\d+)', log())[-1] == '0')
                tap('settings'); route('settings_page')
                tap('settings_display'); route('display_page')
                set_level(418, 100)
                shot('display-restored-100')
                tap('display_back'); route('settings_page')
                tap('settings_back'); route('home')
                assert shot('home-restored-100') == bright
                assert backend.wait(timeout=20) == 0
                call('quit')
                assert host.wait(timeout=6) == 0
            assert 'ECHOY_UI_ERROR' not in log() and 'cleanup_error=0' in log()
            result = {'passed': ['manual 60% brightness survives more than 61 idle seconds',
                                 'native touch preserves 60% without starting playback',
                                 'manual 60% brightness survives more than 61 playing seconds',
                                 'native slider restores 100% brightness'],
                      'elapsed_idle_seconds': idle_seconds,
                      'elapsed_playing_seconds': playing_seconds,
                      'scope': 'production C UI in official GSP; physical panel not tested'}
            (out / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result, indent=2))
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=6)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()


if __name__ == '__main__':
    main()
