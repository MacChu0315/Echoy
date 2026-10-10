"""Use real elapsed time and native GSP input to verify display idle behavior."""
import argparse
import json
import os
from pathlib import Path
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

                time.sleep(.15)
                call('wait', {'frames': 5})
                call('tap_object', {'name': 'transport'})
                until(lambda: 'ECHOY_STATE phase=5' in log())
                began = time.monotonic()
                until(lambda: time.monotonic() - began > 61, 64)
                bright = shot('playing-after-minute')
                assert 'effective=20' not in log(), 'Playback incorrectly dimmed'
                call('tap_object', {'name': 'strip_action'})
                until(lambda: 'ECHOY_STATE phase=0' in log().split('ECHOY_STATE phase=5')[-1])
                until(lambda: 'effective=20' in log(), 63)
                dark = shot('idle-after-minute')
                assert sum(dark) < sum(bright) * .3, (bright, dark)
                before = log().count('ECHOY_STATE phase=')
                for pressed in (True, False):
                    call('feed_pointer', {'x': 240, 'y': 80, 'pressed': pressed})
                    call('wait', {'frames': 3})
                until(lambda: log().rstrip().endswith('effective=100 auto_dim=1 ok=1'))
                assert shot('touch-restored') == bright
                assert log().count('ECHOY_STATE phase=') == before, 'Neutral wake changed music state'
                assert backend.wait(timeout=20) == 0
                call('quit')
                assert host.wait(timeout=6) == 0
            assert 'ECHOY_UI_ERROR' not in log() and 'cleanup_error=0' in log()
            result = {'passed': ['61 seconds of playback remain bright', '60 seconds idle dim to 20%',
                                 'native touch restores brightness without starting playback'],
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
