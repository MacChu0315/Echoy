#!/usr/bin/env python3
"""Three independent group/band/PCM owners with official native GSP inputs.

Only external physical/radio/clock adapters are simulated. This is no hardware
update or speaker/RF acceptance. Keep every failed run's separate evidence.
"""
import argparse
from array import array
from contextlib import ExitStack
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import time
import wave
from check_desk import DeskCheck, ROOT, TILES, pcm_statistics

class ChainCheck(DeskCheck):
    def joined(self, count=3):
        def ready(s):
            owners=s.get('independent_owners',[])
            return self.stable(s) and len(owners)==3 and all(
                o['count']==count and o['mapped'] and o['operations']<=6 and o['phase']==5
                for o in owners)
        return self.until(ready,'Independent group did not finish preparation/activation',timeout=12)

    def silent(self):
        self.command('pause_all')
        self.until(lambda s: all(t['phase']==0 and not t['loop'] for t in s['tiles'].values()),'Pause did not stay local')
        time.sleep(.6)
        assert all(o['energy']==0 for o in self.status()['independent_owners'])

    def exercise(self):
        state=self.status()
        reduced=os.environ.get('ECHOY_REDUCED_MOTION')=='1'
        assert all(t['reduced_motion']==reduced for t in state['tiles'].values())
        assert [o['offset_us'] for o in state['independent_owners']]==[0,70000000,140000000]
        assert all(t['events']==0 and not t['loop'] for t in state['tiles'].values())
        for tile in TILES: self.shot(tile,'initial')
        start=self.mark('ordinary-pad-begin')['sample_index']
        self.pad('A'); time.sleep(.5)
        self.windows['ordinary-pad']=[start,self.mark('ordinary-pad-end')['sample_index']]
        self.passed('Three native UI hosts read three separately allocated actual musical owners and different clocks')
        time.sleep(1.1)
        slow=self.mark('slow-preview-begin')['sample_index']
        self.pad('A'); time.sleep(1.35)
        self.windows['slow-preview-tail']=[slow+12000,slow+26400]
        self.pad('A'); time.sleep(.12)
        fast=self.mark('fast-preview-begin')['sample_index']
        self.pad('A'); time.sleep(.5)
        self.windows['fast-preview-silence']=[fast+7200,self.mark('fast-preview-end')['sample_index']]
        assert all(t['phase']==0 and not t['loop'] and not t['events'] for t in self.status()['tiles'].values())
        self.passed('Actual native slow taps give longer replies; fast taps retire them without recording or autoplay')
        start=self.mark('example-begin')['sample_index']
        for tile in TILES: self.command('example '+tile)
        self.until(lambda s: all(t['phase']==0 and t['events'] for t in s['tiles'].values()) and self.stable(s),
                   'Solo example replies did not finish',timeout=8)
        self.windows['example-once']=[start,self.mark('example-end')['sample_index']]
        self.tap('B','role'); self.visual_ready('B','role'); self.tap('B','next_role')
        self.until(lambda s: s['tiles']['B']['role']=='voice' and self.stable(s),'Native B role did not change')
        self.tap('B','role_back'); self.visual_ready('B','home')
        self.command('edges A B B C'); linked=self.joined()
        assert linked['tiles']['B']['source']=='A' and linked['tiles']['C']['source']=='B'
        assert len({o['group'] for o in linked['independent_owners']})==1
        assert all(not t['loop'] and t['events'] for t in linked['tiles'].values())
        self.export('linked-silent')
        self.passed('Actual A→B→C candidate/READY/ARM/authorization/activation completes while every tile stays paused')
        # An empty/paused follower can try its own voice without a group edit.
        before_preview=self.status()
        for n in range(4):
            start=self.mark(f'random-pad-{n}-begin')['sample_index']
            self.pad('B'); time.sleep(.5)
            self.windows[f'random-pad-{n}']=[start,self.mark(f'random-pad-{n}-end')['sample_index']]
        preview=self.status()
        assert preview['independent_owners'][0]['energy']==0 and preview['independent_owners'][2]['energy']==0
        assert all(not t['loop'] and t['phase']==0 for t in preview['tiles'].values())
        assert [o['attempt'] for o in preview['independent_owners']]==[o['attempt'] for o in before_preview['independent_owners']]
        self.silent()
        self.passed('Native follower pad sounds only locally, keeps the group transaction and Pause clears it')
        # Actual local record feedback must not wait for the distributed commit.
        self.record_ready('A'); self.tap_at('A',170,420)
        self.until(lambda s: s['tiles']['A']['phase']==1,'Root Record did not count in')
        self.until(lambda s: s['tiles']['A']['phase']==2,'Root Record did not capture',timeout=5)
        self.pad('A')
        reply=self.until(lambda s: s['tiles']['A']['phase']==4,'Local recorded reply did not start',timeout=5)
        assert not any(t['loop'] for t in reply['tiles'].values())
        assert any(o['phase']!=5 for o in reply['independent_owners']),reply['independent_owners']
        self.until(lambda s: s['tiles']['A']['phase']==0 and self.stable(s),
                   'Local reply did not finish exactly once',timeout=10)
        self.joined()
        self.passed('Native root recorded reply starts before group completion; followers stay paused')
        for first in TILES:
            self.silent(); self.play_ready(first); self.tap(first,'transport')
            self.until(lambda s: s['tiles'][first]['loop'] and s['tiles'][first]['phase']==5,'First native Play did not start')
            assert all(not t['loop'] for tile,t in self.status()['tiles'].items() if tile!=first)
            for tile in TILES:
                if tile!=first:
                    self.play_ready(tile); self.tap(tile,'transport')
            self.until(lambda s: all(t['loop'] and t['phase']==5 for t in s['tiles'].values()),'Later native Play did not join')
            time.sleep(.6)
            clocks=[t['clock_origin_us'] for t in self.status()['tiles'].values()]
            assert max(clocks)-min(clocks)<10000,clocks
        self.passed('A/B/C-first real native Play, later entry, repeated Pause and clock agreement never grant peer permission')
        start=self.mark('middle-edit-begin')['sample_index']
        self.tap('B','role'); self.visual_ready('B','role'); self.tap('B','phrase_3')
        changed=self.until(lambda s: self.stable(s) and s['tiles']['B']['phrase']==3,'B/C edit did not activate',timeout=12)
        assert all(t['loop'] for t in changed['tiles'].values())
        self.tap('B','role_back'); self.visual_ready('B','home')
        time.sleep(2.6)
        self.windows['middle-continues']=[start,self.mark('middle-edit-end')['sample_index']]
        self.export('middle-edited'); self.shot('B','following-playing')
        self.passed('Native middle phrase edit flows to C through the real protocol while existing PCM continues')
        before=self.export('before-release'); self.command('edges B C')
        self.until(lambda s: self.stable(s) and s['tiles']['B']['source'] is None and s['tiles']['C']['source']=='B',
                   'Removing A did not preserve B—C',timeout=12)
        released=self.export('after-release')
        for tile in TILES:
            a={k:v for k,v in before['tiles'][tile]['current'].items() if k not in ('source','detached')}
            b={k:v for k,v in released['tiles'][tile]['current'].items() if k not in ('source','detached')}
            assert a==b,(tile,a,b)
            assert self.status()['tiles'][tile]['loop']
        self.command('edges'); self.until(lambda s: self.stable(s) and all(t['source'] is None for t in s['tiles'].values()),
                                          'Full physical release did not settle')
        self.passed('Releasing A retains exact current phrases/playback and makes B the root of B—C; full release keeps local playback')
        self.silent()
        # B—C remains active while A waits for B's native recording/reply.
        self.command('edges B C')
        self.until(lambda s: self.stable(s) and s['tiles']['C']['source']=='B','B—C did not rejoin',timeout=12)
        self.record_ready('B'); self.tap_at('B',170,420)
        self.until(lambda s: s['tiles']['B']['phase']==1,'Native Record did not count in')
        self.command('edges A B B C')
        self.until(lambda s: s['tiles']['B']['phase']==2,'Deferred native count-in was cancelled',timeout=5)
        assert self.status()['tiles']['B']['source'] is None
        self.pad('B')
        self.until(lambda s: s['tiles']['B']['phase']==4,'Recorded reply did not run',timeout=6)
        self.until(lambda s: s['tiles']['B']['phase']==0 and s['tiles']['B']['source']=='A' and self.stable(s),
                   'A was not admitted after B completed its recorded reply',timeout=12)
        assert not any(t['loop'] for t in self.status()['tiles'].values())
        self.shot('B','following-play-only'); self.passed('Actual native 3/2/1 recording and one-shot reply finish before upstream admission; no autoplay')
        self.silent()
        start=self.mark('paused-silence-begin')['sample_index']+4096
        time.sleep(.8)
        self.windows['paused-silence']=[start,self.mark('paused-silence-end')['sample_index']]
        for tile in TILES: self.shot(tile,'final')
        self.export('final')

    def finish(self):
        deadline=self.backend_started+self.args.duration_ms/1000+8
        while self.backend.poll() is None and time.monotonic()<deadline: time.sleep(.1)
        assert self.backend.poll()==0,'Native independent-owner cleanup failed'
        self.stop_sampler.set()
        if self.sampler: self.sampler.join(timeout=2)
        for tile in TILES: self.call(tile,'quit')
        for child in self.processes[:-1]: assert child.wait(timeout=6)==0
        log=(self.output/'desk.log').read_text()
        assert re.search(r'ECHOY_DESK_CLOSED error=0 samples=\d+ late_blocks=0\b',log),'Native PCM missed its deadline'
        for tile in TILES:
            assert f'ECHOY_DESK_UI tile={tile} closed=1' in log
            host=(self.output/f'host-{tile}.log').read_text()
            assert 'session complete' in host and not re.search(r'\bERROR\b|^E \(',host,re.MULTILINE)
        for bad in ('ECHOY_UI_ERROR','ECHOY_DESK_UI_ERROR','ECHOY_AUDIO_PREPARE_FAILED',
                    'ECHOY_DESK_COMMAND_REJECTED','No free canvas slot','Failed to decode'):
            assert bad not in log,bad
        with wave.open(str(self.wav),'rb') as wav:
            assert wav.getframerate()==24000 and wav.getnchannels()==1 and wav.getsampwidth()==2
            samples=array('h',wav.readframes(wav.getnframes()))
        if sys.byteorder!='little': samples.byteswap()
        metrics={'all':pcm_statistics(samples)}
        for name,window in self.windows.items(): metrics[name]=pcm_statistics(samples,*window)
        assert metrics['all']['clipped']==0
        for name in ('ordinary-pad','example-once','middle-continues','slow-preview-tail',*[f'random-pad-{n}' for n in range(4)]): assert metrics[name]['nonzero']>100,metrics[name]
        assert metrics['paused-silence']['nonzero']==0
        history=[json.loads(line) for line in (self.output/'status-history.jsonl').read_text().splitlines()]
        assert all(len(row['status']['independent_owners'])==3 and all(
            o['operations']<=6 for o in row['status']['independent_owners']) for row in history)
        self.passed('Official native hosts and three actual C owners exit cleanly; PCM has no late blocks/clipping and paused output is exactly silent')
        (self.output/'audio-metrics.json').write_text(json.dumps({'frames':len(samples),'sample_rate':24000,
            'metrics':metrics,'scope':'actual independent C owner PCM; no speaker or RF acceptance'},indent=2)+'\n')
        assert metrics['fast-preview-silence']['nonzero']==0,metrics['fast-preview-silence']
        (self.output/'checks.json').write_text(json.dumps({'passed':self.checks,
            'scope':'official GSP 1.5.1, native pointer input, three actual v3/group/band/PCM owners, simulated physical/radio/clock adapters'},indent=2)+'\n')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build-dir',type=Path,default=ROOT/'output/build-sim-three-independent')
    parser.add_argument('--sim',type=Path,default=ROOT.parent/'.toolchains/gsp-sim-1.5.1/sim')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--duration-ms',type=int,default=110000)
    args=parser.parse_args(); args.desk_binary=args.build_dir/'echoy_chain_desk'
    assert 90000<=args.duration_ms<=180000
    output=args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
    assert not any(output.iterdir()),'Preserve earlier evidence in a different output directory'
    with tempfile.TemporaryDirectory(prefix='echoy-three-native-') as runtime:
        check=ChainCheck(args,output,Path(runtime))
        try:
            with ExitStack() as stack:
                check.setup(stack); check.exercise(); check.finish()
        except BaseException as error:
            (output/'failure.json').write_text(json.dumps({'error':str(error),'state':check.status(),'passed':check.checks},indent=2)+'\n')
            raise
        finally: check.close()
if __name__=='__main__': main()
