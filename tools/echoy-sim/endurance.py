#!/usr/bin/env python3
"""Wall-clock PC endurance, never a hardware or acoustic acceptance result."""
import argparse
import base64
import importlib.util
import json
from pathlib import Path
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('echoy_sim', HERE / 'run.py')
sim = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sim)


def rss(pid):
    for line in Path(f'/proc/{pid}/status').read_text().splitlines():
        if line.startswith('VmRSS:'):
            return int(line.split()[1])
    raise RuntimeError('No RSS observation for C process')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('--output', type=Path, default=sim.ROOT/'output/sim/endurance')
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error('--seconds must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    rows = args.output/'samples.jsonl'
    started = time.monotonic()
    total = touches = nonzero = 0
    latencies = []
    peak_rss = [0, 0, 0]
    report = {'scope': 'PC development simulation only', 'requested_wall_seconds': args.seconds,
              'hardware_connected': False, 'passed': False, 'status': 'running'}
    (args.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
    try:
        with sim.Bridge() as bridge, rows.open('w') as log:
            bridge.request({'op': 'power', 'id': 'C', 'on': True})
            bridge.advance(400)
            for name in sim.IDS:
                bridge.request({'op': 'touch', 'id': name, 'phase': 0, 'x': 330, 'y': 425})
                bridge.request({'op': 'touch', 'id': name, 'phase': 2, 'x': 330, 'y': 425})
            bridge.advance(700)
            bridge.request({'op': 'contacts', 'edges': [['A', 'B'], ['B', 'C']], 'uncertain': []})
            pids = [r.process.pid for r in bridge.runners]
            baseline = [rss(pid) for pid in pids]
            previous = [0, 0, 0]
            simulated_start = bridge.time_us
            pace_start = time.monotonic()
            while time.monotonic()-started < args.seconds:
                target = simulated_start + int((time.monotonic()-pace_start)*1000)*1000
                if target > bridge.time_us:
                    bridge.advance((target-bridge.time_us)//1000)
                # Actual shared LVGL hit testing, with movement and release.
                index = total % 3
                for phase, y in ((0, 275), (1, 230 if total % 2 else 300), (2, 230 if total % 2 else 300)):
                    bridge.call(index, f'P {phase} 200 {y}')
                    touches += 1
                if total % 15 == 0:
                    for i in range(3):
                        bridge.call(i, f'L {int((total//15) % 2)}')
                before = time.monotonic()
                answer = bridge.publish()
                latency = (time.monotonic()-before)*1000
                latencies.append(latency)
                memory = [rss(pid) for pid in pids]
                for i, d in enumerate(answer['devices']):
                    assert d['present'] and d['transport'] == 'playing', {k: d[k] for k in ('uid', 'present', 'transport')}
                    assert d['revision'] >= previous[i]
                    assert d['revision'] == d['frame_revision'] == d['audio_revision']
                    assert base64.b64decode(d['frame'].split(',')[1]).startswith(b'\x89PNG')
                    if any(base64.b64decode(d['pcm'])):
                        nonzero += 1
                    previous[i] = d['revision']
                    peak_rss[i] = max(peak_rss[i], memory[i])
                    assert bridge.runners[i].process.poll() is None
                log.write(json.dumps({'wall_seconds': time.monotonic()-started,
                                      'simulation_us': bridge.time_us, 'publication_ms': latency,
                                      'rss_kib': memory, 'queue_depth': len(bridge.queue),
                                      'devices': [{k: d[k] for k in ('uid', 'boot', 'revision', 'transport', 'role', 'phrase', 'activity', 'members', 'rejected')} for d in answer['devices']]})+'\n')
                log.flush()
                total += 1
                time.sleep(min(1.0, max(0, args.seconds-(time.monotonic()-started))))
            assert total > 0 and nonzero > 0
            report.update(passed=True, status='complete', c_processes=3, pids=pids, initial_rss_kib=baseline,
                          final_rss_kib=memory, peak_rss_kib=peak_rss, samples=total,
                          lvgl_touch_events=touches, nonzero_pcm_publications=nonzero,
                          publication_p50_ms=sorted(latencies)[len(latencies)//2],
                          publication_p95_ms=sorted(latencies)[int((len(latencies)-1)*.95)],
                          publication_max_ms=max(latencies), final_simulation_us=bridge.time_us)
        report['c_processes_exited'] = all(r.process.poll() == 0 for r in bridge.runners)
        assert report['c_processes_exited']
    except BaseException as error:
        report.update(passed=False, status='failed', error=repr(error))
        raise
    finally:
        report['actual_wall_seconds'] = time.monotonic()-started
        report['unmeasured'] = ['MCU heap/stack/CPU', 'DMA underruns', 'hardware touch latency',
                                'Iris mirror overhead', 'speaker output', 'wireless/acoustic skew']
        (args.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
