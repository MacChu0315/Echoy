#!/usr/bin/env python3
"""Input-driven native GSP integration checks, including generated phrase data."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
from PIL import Image, ImageChops
from check_home import snapshot_backend, assert_v4_geometry, compact_message, assert_header_fit
ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build-dir', type=Path, default=ROOT/'projects/echoy/build-sim-v4')
    p.add_argument('--duration-ms',type=int,default=60000)
    p.add_argument('--output', type=Path, default=ROOT/'output/v4-full-ui-check')
    a = p.parse_args(); out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    assert 45000<=a.duration_ms<=240000,'Use a finite duration covering all pages'
    manifest = snapshot_backend(a.build_dir,out)
    assert_v4_geometry(json.loads(Path(manifest['api']).read_text()))
    sys.path.insert(0, str(Path(manifest['component_root'])/'tools/sim_bridge'))
    from rpc_client import connect
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
    ready = out/'ready.json'; ready.unlink(missing_ok=True)
    processes = []; checks = []
    def until(test, message, timeout=8):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if test(): return
            if any(x.poll() is not None for x in processes): raise AssertionError('process exited: '+message)
            time.sleep(.02)
        raise AssertionError(message)
    with tempfile.TemporaryDirectory(prefix='echoy-full-') as control, (out/'host.log').open('w') as hf, (out/'backend.log').open('w') as bf:
        os.chmod(control, 0o700)
        try:
            sim=ROOT.parent/'.toolchains/gsp-sim-1.5.1/sim'
            host=subprocess.Popen([str(sim),'--bundle',manifest['bundle'],'--api-json',str(Path(manifest['bundle']).with_name('bundle_gsp.api.json')),'--backend-enable','--backend-required','--api-listen',f'tcp://127.0.0.1:{port}','--headless','--fps','1000','--frames','0','--ready-file',str(ready)],stdout=hf,stderr=subprocess.STDOUT)
            processes.append(host); until(ready.exists,'host readiness')
            info=json.loads(ready.read_text()); assert info['pid']==host.pid
            env={**os.environ,'ECHOY_SIM_CONTROL_DIR':control,'ECHOY_SIM_SEED':'44a0'}
            backend=subprocess.Popen([manifest['executable'],'--endpoint',info['backend'],'--duration-ms',str(a.duration_ms)],env=env,stdout=bf,stderr=subprocess.STDOUT)
            processes.append(backend)
            def log(): return (out/'backend.log').read_text()
            until(lambda:'ECHOY_READY' in log(),'backend readiness')
            with connect(port=port) as api:
                def call(m,args=None):
                    r=api.call(m,args); assert 'error' not in r,r
                    return r.get('result')
                def settle(): time.sleep(.13); call('wait',{'frames':5})
                def tap(name): call('tap_object',{'name':name}); settle()
                def tap_at(x,y): call('tap',{'x':x,'y':y});settle()
                def shot(name):
                    settle(); path=out/(name+'.png'); call('screenshot',{'path':str(path),'format':'png'})
                    im=Image.open(path).convert('RGB'); assert im.size==(480,480); return im
                def drag(x,y1,y2):
                    call('feed_pointer',{'x':x,'y':y1,'pressed':True});call('wait',{'frames':2})
                    for i in range(1,7):
                        call('feed_pointer',{'x':x,'y':round(y1+(y2-y1)*i/6),'pressed':True});call('wait',{'frames':2})
                    call('feed_pointer',{'x':x,'y':y2,'pressed':False});settle()
                def route(name):
                    until(lambda:re.findall(r'ECHOY_ROUTE route=(\w+)',log())[-1]==name, 'route '+name)
                def command(value):
                    with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as sock: sock.sendto(value.encode(),str(Path(control)/'command.sock'))
                    settle()
                initial=shot('pulse-home'); route('home')
                tap('role'); route('role_page')
                before=len(re.findall(r'ECHOY_STATE phase=',log()))
                tap('gallery_transport')
                until(lambda:len(re.findall(r'ECHOY_STATE phase=',log()))>before and re.findall(r'ECHOY_STATE phase=(\d+)',log())[-1]=='4','gallery audition started')
                until(lambda:re.findall(r'ECHOY_STATE phase=(\d+)',log())[-1]=='0','gallery audition finished',timeout=6)
                assert 'ECHOY_STATE phase=5' not in log()
                checks.append('Role-page Hear once completes once and returns to idle')
                gallery_images=[]; home_headers=[]
                for r in ['pulse','voice','dust','harmony']:
                    for phrase in [1,2,3]:
                        previous_revision=int(re.findall(r'ECHOY_PLAN revision=(\d+)',log())[-1])
                        tap(f'phrase_{phrase}')
                        def committed():
                            rows=re.findall(r'ECHOY_PLAN revision=(\d+) role=(\w+) phrase=(\d+) anchors=(\d+) events=(\d+)',log())
                            rev,role,ph,anchors,_=rows[-1]
                            return int(rev)>previous_revision and role==r and int(ph)==phrase and anchors=='4'
                        until(committed,f'{r}/{phrase} committed')
                        revision=re.findall(r'ECHOY_PLAN revision=(\d+)',log())[-1]
                        until(lambda:f'ECHOY_GALLERY revision={revision}\n' in log(),f'{r}/{phrase} images published')
                        im=shot(f'gallery-{r}-{phrase}'); gallery_images.append(im.crop((137,108,343,209)))
                        tap('role_back');route('home');home=shot(f'home-zh-{r}-{phrase}')
                        assert_header_fit(home,r,'zh')
                        assert not compact_message(home,home.getpixel((100,80))), 'Steady header should not retain a status caption'
                        home_headers.append({'role':r,'phrase':phrase,'locale':'zh','file':f'home-zh-{r}-{phrase}.png'})
                        tap('role');route('role_page')
                    tap('role_back');route('home');shot(r+'-home')
                    tap('role');route('role_page')
                    tap('next_role')
                assert all(ImageChops.difference(gallery_images[0], im).getbbox() for im in gallery_images[3:]),'Role graphics did not update'
                # A real source phrase must add accompaniment, not just repeat raw tap bits.
                plans=re.findall(r'ECHOY_PLAN revision=\d+ role=(\w+) phrase=(\d+) anchors=(\d+) events=(\d+)',log())
                assert len({(r,p) for r,p,_,_ in plans})==12,plans
                assert any(int(events)>int(anchors) for _,_,anchors,events in plans)
                checks.append('All four roles and twelve expressions commit real generated plans and render gallery images')
                tap('role_back');route('home');tap('settings');route('settings_page');shot('settings-top-zh')
                tap('settings_guide');route('guide_page');before=shot('guide-top-zh');drag(230,418,170);after=shot('guide-bottom-zh')
                assert ImageChops.difference(before,after).getbbox();route('guide_page');tap('guide_back');route('settings_page')
                # Info is visible after a partial scroll; scrolling must not activate rows.
                drag(430,420,280);route('settings_page');tap_at(230,298);route('info_page');shot('info-zh');tap('info_back');route('settings_page')
                drag(430,425,135);route('settings_page');shot('settings-bottom-zh')
                tap_at(230,182);until(lambda:(Path(control)/'session.json').exists(),'export')
                exported=json.loads((Path(control)/'session.json').read_text());(out/'session.json').write_text(json.dumps(exported,ensure_ascii=False,indent=2)+'\n')
                tile=exported['tiles']['A'];assert len(tile['ownSeed'])==4;assert len(tile['current']['full'])>=4
                tap_at(388,293);until(lambda:'ECHOY_LOCALE en' in log(),'English locale');assert 'ECHOY_LOCALE en' in log();shot('settings-bottom-en')
                drag(430,145,440);drag(430,145,300);route('settings_page');shot('settings-top-en')
                tap('settings_guide');route('guide_page');shot('guide-top-en');drag(230,420,170);shot('guide-bottom-en');tap('guide_back');route('settings_page')
                tap('settings_back');route('home');shot('home-en')
                tap('role');route('role_page')
                for r in ['pulse','voice','dust','harmony']:
                    for phrase in [1,2,3]:
                        old=int(re.findall(r'ECHOY_PLAN revision=(\d+)',log())[-1]);tap(f'phrase_{phrase}')
                        until(lambda: int(re.findall(r'ECHOY_PLAN revision=(\d+)',log())[-1])>old, f'English {r}/{phrase} commit')
                        tap('role_back');route('home');home=shot(f'home-en-{r}-{phrase}')
                        assert_header_fit(home,r,'en')
                        assert not compact_message(home,home.getpixel((100,80)))
                        home_headers.append({'role':r,'phrase':phrase,'locale':'en','file':f'home-en-{r}-{phrase}.png'})
                        tap('role');route('role_page')
                    tap('next_role')
                tap('role_back');route('home')
                (out/'home-header-captures.json').write_text(json.dumps(home_headers,indent=2)+'\n')
                checks.append('All twelve expressions have Chinese/English v4 home header captures and hidden steady-state captions')
                checks.append('Menus, bounded scrolling, Chinese/English text and real session export work through pointer input')
                for state in ['fault','loading','empty']:
                    command(state);route('status_page');shot('status-'+state)
                    tap('recover');route('home')
                checks.append('Mock fault/loading/empty states recover to a paused home')
                tap('settings');drag(430,425,115);drag(430,425,250);route('settings_page');tap_at(230,404);route('removed');shot('removed')
                assert json.loads((Path(control)/'status.json').read_text())['removed']
                command('restore');route('home');shot('restored');assert not json.loads((Path(control)/'status.json').read_text())['removed']
                checks.append('Put-away and external restore preserve the phrase')
                code=backend.wait(timeout=a.duration_ms/1000+5);assert code==0,code
                call('quit');assert host.wait(timeout=6)==0
            logs=log()+(out/'host.log').read_text()
            for error in ['ECHOY_UI_ERROR','No free canvas slot','Failed to decode','cleanup_error=-']:
                assert error not in logs,error
            assert 'last_error=0' in logs and 'cleanup_error=0' in logs
            (out/'checks.json').write_text(json.dumps({'passed':checks,'scope':'native UI and musical data; no audio/hardware acceptance'},ensure_ascii=False,indent=2)+'\n')
            print(json.dumps(checks,ensure_ascii=False,indent=2))
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:process.wait(timeout=6)
                    except subprocess.TimeoutExpired:process.kill();process.wait()
if __name__=='__main__': main()
