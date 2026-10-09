#!/usr/bin/env python3
"""Exercise the real C UI through the official GSP simulator's input-only API."""
import argparse
import json
from pathlib import Path
import re
import shutil
import hashlib
import os
import socket
import subprocess
import sys
import time
from PIL import Image, ImageChops
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'projects/echoy'

def snapshot_backend(build,out):
    """Freeze binary and bundle without touching a live build directory."""
    manifest=json.loads((build/'echoy_backend-Release.json').read_text())
    runtime=out/'runtime';runtime.mkdir(exist_ok=False)
    paths={'executable':Path(manifest['executable']),'bundle':Path(manifest['bundle']),
           'api':Path(manifest['bundle']).with_name('bundle_gsp.api.json')}
    metadata={}
    for key,source in paths.items():
        before=hashlib.sha256(source.read_bytes()).hexdigest();target=runtime/source.name
        shutil.copy2(source,target)
        assert before==hashlib.sha256(target.read_bytes()).hexdigest()==hashlib.sha256(source.read_bytes()).hexdigest(),f'Build changed during snapshot: {source}'
        metadata[key]={'source':str(source),'sha256':before};manifest[key]=str(target)
    (out/'runtime-snapshot.json').write_text(json.dumps(metadata,indent=2)+'\n')
    return manifest

def assert_v4_geometry(api_info):
    objects={o['name']:o for o in api_info['scenes'][0]['objects']}
    expected={'art':(90,89,390,293),'volume':(16,89,80,296),'richness':(400,89,464,296),
              'record':(118,378,240,462),'transport':(240,378,362,462),
              'strip_action':(118,378,362,462),'role':(22,378,106,462),'settings':(374,378,458,462)}
    for name,box in expected.items():
        assert name in objects,f'v4 control {name} is missing'
        got=objects[name]['bounds'];actual=tuple(got[k] for k in ('x1','y1','x2','y2'))
        assert actual==box,(name,actual,box)

def assert_header_fit(image,role=None,locale=None):
    """Catch a second-line title and intrusion into the reserved battery gap."""
    paper=image.getpixel((100,80))
    for box in [(34,63,384,82),(346,25,383,61)]:
        assert not any(max(abs(a-b) for a,b in zip(pixel,paper))>35 for pixel in image.crop(box).getdata()), ('Header wraps or crosses battery clearance',box)
    ink=sum(max(abs(a-b) for a,b in zip(pixel,paper))>35 for pixel in image.crop((34,26,340,61)).getdata())
    assert ink>100,'Home identity did not render'
    if role is not None:
        x={'pulse':103,'voice':102,'dust':93,'harmony':142}[role] if locale=='en' else 94
        assert any(max(abs(a-b) for a,b in zip(pixel,paper))>35 for pixel in image.crop((x+1,38,x+7,47)).getdata()), ('Missing role/phrase middle-dot separator',role,locale)

def compact_message(image,paper):
    return any(max(abs(a-b) for a,b in zip(pixel,paper))>35 for pixel in image.crop((24,338,456,365)).getdata())

def strip_events(log,ready=False,tile='A'):
    prefix='ECHOY_STRIP_READY ' if ready else 'ECHOY_STRIP '
    rows=[]
    for line in log.splitlines():
        if not line.startswith(prefix):continue
        row=dict(re.findall(r'(\w+)=([^\s]+)',line))
        if row.get('tile','A')!=tile:continue
        for key in ('at_us','duration_ms','reduced_motion','record','play','single'):
            if key in row:row[key]=int(row[key])
        rows.append(row)
    return rows

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sim',type=Path,default=ROOT.parent/'.toolchains/gsp-sim-1.5.1/sim')
    parser.add_argument('--output',type=Path,default=ROOT/'output/v4-home-check')
    parser.add_argument('--build-dir',type=Path,default=APP/'build-sim-v4')
    parser.add_argument('--duration-ms',type=int,default=30000)
    parser.add_argument('--build',action='store_true',help='explicitly build the supplied development directory')
    parser.add_argument('--contract-only',action='store_true',help='check v4 entry geometry only, useful for preserved v3 RED evidence')
    parser.add_argument('--reduced-motion',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    assert 25000<=args.duration_ms<=180000,'Use a finite duration covering the input scenario'
    if args.build:
        assert args.build_dir.resolve().name not in ('build-sim-desk-final','build-sim'), 'Never rebuild a live preview from this checker'
        with (out/'build.log').open('w') as build_log:
            subprocess.run(['cmake','--build',str(args.build_dir),'--target','echoy_backend'],check=True,stdout=build_log,stderr=subprocess.STDOUT)
    manifest=snapshot_backend(args.build_dir,out)
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
            '--headless','--fps','1000','--frames','0','--ready-file',str(ready)],stdout=host_log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+10
        while not ready.exists():
            assert host.poll() is None,'Simulator exited before binding its endpoints'
            if time.monotonic()>deadline:raise TimeoutError('Simulator did not report readiness')
            time.sleep(.02)
        info=json.loads(ready.read_text());assert info['pid']==host.pid
        backend=subprocess.Popen([manifest['executable'],'--endpoint',info['backend'],
            '--duration-ms',str(args.duration_ms)],env={**os.environ,**({'ECHOY_REDUCED_MOTION':'1'} if args.reduced_motion else {})},stdout=backend_log,stderr=subprocess.STDOUT)
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
                time.sleep(.04)
                call('wait',{'frames':5})
                p=out/(name+'.png');call('screenshot',{'path':str(p),'format':'png'})
                assert Image.open(p).size==(480,480)
                return p
            def ready_strip(mode=None,**permissions):
                deadline=time.monotonic()+4
                while time.monotonic()<deadline:
                    rows=strip_events((out/'backend.log').read_text(),ready=True)
                    if rows and (mode is None or rows[-1]['mode']==mode) and all(rows[-1].get(k)==v for k,v in permissions.items()):return rows[-1]
                    time.sleep(.01)
                raise AssertionError(('Native strip properties did not become ready',mode,permissions,rows))
            def tap(name):
                if name=='record':ready_strip('idle',record=1)
                elif name=='transport':ready_strip('idle',play=1)
                elif name=='strip_action':ready_strip(single=1)
                return call('tap_object',{'name':name})
            def pad(hold=0):
                call('feed_pointer',{'x':240,'y':189,'pressed':True})
                if hold: time.sleep(hold)
                call('feed_pointer',{'x':240,'y':189,'pressed':True})
                call('feed_pointer',{'x':240,'y':189,'pressed':False})
            phase(0);caps=call('capabilities');assert caps['runtime_version']=='1.5.1'
            assert caps['bridge_media_version']==1 and caps['bridge_pointer_version']==1
            (out/'capabilities.json').write_text(json.dumps(caps,indent=2)+'\n')
            # An input at the removed v3 top button must never arm recording.
            call('tap',{'x':310,'y':42});call('wait',{'frames':5});time.sleep(1)
            assert state_rows()[-1][0]==0,'Removed top Record target still starts recording (v3 behavior)'
            api_info=json.loads(Path(manifest['api']).read_text())
            assert_v4_geometry(api_info)
            initial=Image.open(shot('initial')).convert('RGB')
            paper=initial.getpixel((100,80))
            assert initial.getpixel((110,125))==paper,'Canvas background differs from GSP scene'
            r,g,b=initial.getpixel((241,189));assert r>140 and r>g*1.6,'v4 Canvas must move to y=89'
            edge=[max(abs(a-b) for a,b in zip(initial.getpixel((x,89)),paper))>12 for x in range(120,360)]
            assert sum(a!=b for a,b in zip(edge,edge[1:]))>20,'Empty pad border must be dashed at y=89'
            assert_header_fit(initial,'pulse','zh')
            checks.append('v4 pad, sliders and 84px bottom controls replace the removed top target')
            if args.contract_only:
                (out/'checks.json').write_text(json.dumps({'passed':checks,'scope':'v4 geometry and removed-target contract'},indent=2)+'\n')
                print(json.dumps(checks,ensure_ascii=False));return
            pad();call('wait',{'frames':10});assert state_rows()[-1][0]==0
            call('tap',{'x':300,'y':420});call('wait',{'frames':5})
            assert state_rows()[-1][0]==0,'Empty Play must remain disabled'
            checks.append('Ordinary pad touches audition only and empty Play remains disabled')
            # No frame wait between clicks: hit the newly active full width while morphing.
            ready_strip('idle',record=1);time.sleep(.3);n=len(state_rows());strip_before=len(strip_events((out/'backend.log').read_text()));call('tap',{'x':179,'y':420});time.sleep(.04)
            call('tap',{'x':346,'y':420});phase(0,after=n)
            rapid=state_rows()[n:];assert any(r[0]==1 for r in rapid),rapid
            modes=strip_events((out/'backend.log').read_text())[strip_before:]
            entered=next(r for r in modes if r['mode']=='cancel');ended=next(r for r in modes if r['mode']=='idle')
            assert entered.get('duration_ms')==(0 if args.reduced_motion else 240),entered
            assert 0<=ended['at_us']-entered['at_us']<240000,('Cancel was not exercised inside 240ms morph',modes)
            time.sleep(1.4);assert state_rows()[-1][0]==0
            checks.append('Right-end Cancel works within 240ms and no delayed count-in restarts')
            tap('record');phase(1);time.sleep(.27);before=shot('countin')
            assert Image.open(before).getpixel((370,301))[:3]==paper,'Count-in must not show recording progress'
            call('tap',{'x':64,'y':420});call('tap',{'x':416,'y':420})
            assert not re.search(r'ECHOY_ROUTE route=(role_page|settings_page)',(out/'backend.log').read_text()),'Navigation was active during capture'
            call('feed_pointer',{'x':432,'y':196,'pressed':True})
            call('feed_pointer',{'x':432,'y':111,'pressed':True})
            call('feed_pointer',{'x':432,'y':111,'pressed':False})
            after=shot('countin-richness-locked');box=(400,89,464,296)
            assert ImageChops.difference(Image.open(before).crop(box),Image.open(after).crop(box)).getbbox() is None
            phase(2);time.sleep(.7);pad(hold=.4);recording=Image.open(shot('recording-leading-silence')).convert('RGB')
            assert max(abs(a-b) for a,b in zip(recording.getpixel((370,301)),paper))>8
            assert sum(r>140 and r>g*1.6 for r,g,b in recording.crop((96,315,388,330)).getdata())>5
            row=phase(4);seed=row[1];assert seed and seed&15==0 and seed.bit_count()==1,hex(seed)
            time.sleep(.27);preview=Image.open(shot('preview')).convert('RGB')
            assert max(abs(a-b) for a,b in zip(preview.getpixel((370,301)),paper))>8
            assert compact_message(preview,paper),'One-shot must have a compact status message'
            phase(0);assert all(row[0]!=5 for row in state_rows())
            time.sleep(.27);idle=Image.open(shot('one-shot-finished')).convert('RGB');assert not compact_message(idle,paper),'Steady idle must hide instructional text'
            checks.append('Capture retains leading silence, held input stays single, one-shot shows full Pause then returns idle')
            # Request another finite audition through the existing settings row,
            # then pause it using the far end of the full-width home control.
            tap('settings')
            deadline=time.monotonic()+4
            while not re.findall(r'ECHOY_ROUTE route=(\w+)',(out/'backend.log').read_text())[-1]=='settings_page':
                assert time.monotonic()<deadline,'Settings route did not open';time.sleep(.01)
            n=len(state_rows());tap('settings_audition');phase(4,after=n)
            n=len(state_rows());call('tap',{'x':346,'y':420});row=phase(0,after=n)
            assert row[1]==seed;shot('one-shot-paused')
            checks.append('Full-width Pause stops a finite audition without enabling a loop or recording')
            ready_strip('idle',play=1);time.sleep(.3);n=len(state_rows());strip_before=len(strip_events((out/'backend.log').read_text()));call('tap',{'x':300,'y':420});time.sleep(.04)
            call('tap',{'x':134,'y':420});phase(0,after=n)
            rapid=state_rows()[n:];modes=strip_events((out/'backend.log').read_text())[strip_before:]
            entered=next(r for r in modes if r['mode']=='pause');ended=next(r for r in modes if r['mode']=='idle')
            assert entered.get('duration_ms')==(0 if args.reduced_motion else 240),entered
            assert 0<=ended['at_us']-entered['at_us']<240000,('Pause was not exercised inside 240ms morph',modes)
            assert state_rows()[-1][1]==seed
            checks.append('Left-end Pause works within 240ms without granting recording permission')
            n=len(state_rows());tap('transport');phase(5,after=n)
            deadline=time.monotonic()+4
            while True:
                playing=Image.open(shot('playing')).convert('RGB')
                if not compact_message(playing,paper):break
                assert time.monotonic()<deadline,'Steady playback must hide instructional text after its shared beat'
                time.sleep(.05)
            n=len(state_rows());tap('strip_action');phase(0,after=n);shot('paused')
            n=len(state_rows());tap('record');phase(1,after=n)
            n=len(state_rows());tap('strip_action');row=phase(0,after=n);assert row[1]==seed
            shot('cancelled')
            n=len(state_rows());tap('record');phase(1,after=n);row=phase(0,after=n,timeout=6)
            assert row[1]==seed;shot('empty-record')
            checks.append('Full-width Pause/Cancel and empty recording retain the old rhythm and do not autoplay')
            call('feed_pointer',{'x':48,'y':196,'pressed':True})
            call('feed_pointer',{'x':48,'y':106,'pressed':True})
            call('feed_pointer',{'x':48,'y':106,'pressed':False})
            p=shot('volume-max');rgb=Image.open(p).getpixel((48,138));assert min(rgb[:3])>230,rgb
            checks.append('Relative volume drag reaches its upper bound at the v4 geometry')
            # The supported duration boundary exits between poll calls. A POSIX
            # signal can interrupt an in-flight synchronous bridge RPC.
            code=backend.wait(timeout=args.duration_ms/1000+5)
            assert code==0,f'Backend cleanup failed: {code}'
            call('quit');assert host.wait(timeout=6)==0
        logs=(out/'backend.log').read_text()+(out/'host.log').read_text()
        assert 'ECHOY_UI_ERROR' not in logs,logs
        assert 'last_error=0' in logs,logs
        transitions=strip_events((out/'backend.log').read_text())
        assert transitions,'No native strip transition evidence'
        assert all(r.get('reduced_motion')==int(args.reduced_motion) for r in transitions),transitions
        if args.reduced_motion:assert all(r.get('duration_ms')==0 for r in transitions),transitions
        else:assert any(r.get('duration_ms')==240 for r in transitions),transitions
        (out/'strip-events.json').write_text(json.dumps({'transitions':transitions,'ready':strip_events((out/'backend.log').read_text(),ready=True)},indent=2)+'\n')
        assert 'cleanup_error=0' in logs and not re.search(r'cleanup_error=(?!0\b)',logs),logs
        result={'passed':checks,'seed':seed,'scope':'native GSP v4 home controls; standalone audio service simulated; no device validation','reduced_motion_requested':args.reduced_motion,'limitations':['Native keyboard focus traversal is not exposed by the GSP pointer API']}
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
