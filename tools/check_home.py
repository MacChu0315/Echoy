#!/usr/bin/env python3
"""Exercise the real C UI through the official GSP simulator's input-only API."""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
from PIL import Image, ImageChops
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'projects/echoy'

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sim',type=Path,default=ROOT.parent/'.toolchains/gsp-sim-1.5.1/sim')
    parser.add_argument('--output',type=Path,default=ROOT/'output/native-check')
    parser.add_argument('--build-dir',type=Path,default=APP/'build-sim')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    with (out/'build.log').open('w') as build_log:
        subprocess.run(['cmake','--build',str(args.build_dir),'--target','echoy_backend'],check=True,stdout=build_log,stderr=subprocess.STDOUT)
    manifest=json.loads((args.build_dir/'echoy_backend-Release.json').read_text())
    component=Path(manifest['component_root'])
    sys.path.insert(0,str(component/'tools/sim_bridge'))
    from rpc_client import connect
    # Reserve a distinct local API port. The simulator fails rather than falling
    # back if another process wins the bind between reserve and start.
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    ready=out/'ready.json';ready.unlink(missing_ok=True)
    host_log=(out/'host.log').open('w');backend_log=(out/'backend.log').open('w')
    backend=None;host=None;checks=[]
    try:
        host=subprocess.Popen([str(args.sim),'--bundle',manifest['bundle'],
            '--api-json',str(Path(manifest['bundle']).with_name('bundle_gsp.api.json')),
            '--backend-enable','--backend-required','--api-listen',f'tcp://127.0.0.1:{port}',
            '--headless','--frames','0','--ready-file',str(ready)],stdout=host_log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+10
        while not ready.exists():
            assert host.poll() is None,'Simulator exited before binding its endpoints'
            if time.monotonic()>deadline:raise TimeoutError('Simulator did not report readiness')
            time.sleep(.02)
        info=json.loads(ready.read_text());assert info['pid']==host.pid
        backend=subprocess.Popen([manifest['executable'],'--endpoint',info['backend'],
            '--duration-ms','20000'],stdout=backend_log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+10
        while 'ECHOY_READY' not in (out/'backend.log').read_text():
            assert backend.poll() is None,'Native UI initialization failed'
            if time.monotonic()>deadline:raise TimeoutError('Native UI did not initialize')
            time.sleep(.02)
        def state_rows():
            return [tuple(map(int,(p,int(seed,16),gen,now))) for p,seed,gen,now in re.findall(r'ECHOY_STATE phase=(\d+) seed=([0-9a-f]+) generation=(\d+) at_us=(\d+)',(out/'backend.log').read_text())]
        def phase(expected,after=0,timeout=6):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                assert backend.poll() is None,'Native backend exited'
                rows=state_rows()
                if len(rows)>after and rows[-1][0]==expected:return rows[-1]
                time.sleep(.01)
            raise AssertionError(f'Expected phase {expected}; states={state_rows()}')
        with connect(port=port) as api:
            def call(method,params=None):
                r=api.call(method,params);assert 'error' not in r,r
                if method=='feed_pointer':
                    settled=api.call('wait',{'frames':2});assert 'error' not in settled,settled
                return r.get('result')
            def shot(name):
                call('wait',{'frames':5})
                p=out/(name+'.png');call('screenshot',{'path':str(p),'format':'png'})
                assert Image.open(p).size==(480,480)
                return p
            def tap(name):return call('tap_object',{'name':name})
            def pad(hold=0):
                call('feed_pointer',{'x':240,'y':213,'pressed':True})
                if hold: time.sleep(hold)
                call('feed_pointer',{'x':240,'y':213,'pressed':True})
                call('feed_pointer',{'x':240,'y':213,'pressed':False})
            phase(0);caps=call('capabilities');assert caps['runtime_version']=='1.5.1'
            assert caps['bridge_media_version']==1 and caps['bridge_pointer_version']==1
            (out/'capabilities.json').write_text(json.dumps(caps,indent=2)+'\n')
            initial=Image.open(shot('initial')).convert('RGB')
            paper=initial.getpixel((100,100))
            # Sample inside the input area, away from its rounded dashed border.
            assert initial.getpixel((110,150))==paper,'Canvas background differs from GSP scene'
            r,g,b=initial.getpixel((241,213));assert r>140 and r>g*1.6,'Native Canvas callback has not drawn'
            edge=[max(abs(a-b) for a,b in zip(initial.getpixel((x,113)),paper))>12 for x in range(120,360)]
            assert sum(a!=b for a,b in zip(edge,edge[1:]))>20,'Empty pad border must be dashed'
            checks.append('Canvas color and dashed input boundary match the reference structure')
            pad();call('wait',{'frames':10});assert state_rows()[-1][0]==0
            checks.append('Ordinary pad presses do not start recording')
            tap('record');phase(1);before=shot('countin')
            assert Image.open(before).getpixel((370,318))[:3]==paper,'Count-in must not show recording progress'
            call('feed_pointer',{'x':432,'y':220,'pressed':True})
            call('feed_pointer',{'x':432,'y':135,'pressed':True})
            call('feed_pointer',{'x':432,'y':135,'pressed':False})
            after=shot('countin-richness-locked')
            box=(400,113,464,320)
            assert ImageChops.difference(Image.open(before).crop(box),Image.open(after).crop(box)).getbbox() is None
            checks.append('Richness cannot change during capture')
            phase(2);time.sleep(.7);pad(hold=.4);recording=Image.open(shot('recording-leading-silence')).convert('RGB')
            assert max(abs(a-b) for a,b in zip(recording.getpixel((370,318)),paper))>8
            assert sum(r>140 and r>g*1.6 for r,g,b in recording.crop((96,329,388,344)).getdata())>5,'Recording must show the moving step'
            row=phase(4);seed=row[1];assert seed and seed&15==0 and seed.bit_count()==1,hex(seed)
            preview=Image.open(shot('preview')).convert('RGB')
            assert max(abs(a-b) for a,b in zip(preview.getpixel((370,318)),paper))>8,'One-shot preview must show progress'
            checks.append('Progress and moving beat feedback distinguish count-in, recording and one-shot preview')
            checks.append('Real pointer capture retains an empty first beat and ignores held duplicates')
            phase(0);assert all(row[0]!=5 for row in state_rows());shot('one-shot-finished')
            checks.append('A completed recording previews once and stops')
            n=len(state_rows());tap('transport');phase(5,after=n);shot('playing')
            n=len(state_rows());tap('transport');phase(0,after=n);shot('paused')
            checks.append('Play and pause affect the native output permission')
            n=len(state_rows());tap('record');phase(1,after=n)
            n=len(state_rows());tap('record');row=phase(0,after=n);assert row[1]==seed
            shot('cancelled');checks.append('Cancelling keeps the previous rhythm')
            call('feed_pointer',{'x':48,'y':220,'pressed':True})
            call('feed_pointer',{'x':48,'y':130,'pressed':True})
            call('feed_pointer',{'x':48,'y':130,'pressed':False})
            p=shot('volume-max');rgb=Image.open(p).getpixel((48,162))
            assert min(rgb[:3])>230,rgb
            checks.append('Relative volume drag reaches its upper bound')
            # The supported duration boundary exits between poll calls. A POSIX
            # signal can interrupt an in-flight synchronous bridge RPC.
            code=backend.wait(timeout=22)
            assert code==0,f'Backend cleanup failed: {code}'
            call('quit');assert host.wait(timeout=6)==0
        logs=(out/'backend.log').read_text()+(out/'host.log').read_text()
        assert 'ECHOY_UI_ERROR' not in logs,logs
        assert 'last_error=0' in logs,logs
        assert 'cleanup_error=0' in logs and not re.search(r'cleanup_error=(?!0\b)',logs),logs
        result={'passed':checks,'seed':seed,'scope':'native GSP home UI; audio service simulated; no device validation'}
        (out/'checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        print(json.dumps(result,ensure_ascii=False,indent=2))
    finally:
        for p in (backend,host):
            if p is not None and p.poll() is None:
                p.terminate()
                try:p.wait(timeout=6)
                except subprocess.TimeoutExpired:p.kill();p.wait()
        backend_log.close();host_log.close()
if __name__=='__main__':main()
