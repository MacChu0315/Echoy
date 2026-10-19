"""Production C pointer callbacks and pixels with deliberately delayed owner feedback."""
import argparse,json,os,socket,subprocess,sys,time
from pathlib import Path
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from check_home import snapshot_backend,assert_v4_geometry
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
m=snapshot_backend(a.build_dir.resolve(),out)
sys.path.insert(0,str(Path(m['component_root'])/'tools/sim_bridge'))
from rpc_client import connect
children=[]
def until(test,message):
    end=time.monotonic()+6
    while time.monotonic()<end:
        if test():return
        assert all(c.poll() is None for c in children),message
        time.sleep(.02)
    raise AssertionError(message)
try:
    with socket.socket() as s:s.bind(('127.0.0.1',0));backend_port=s.getsockname()[1]
    with (out/'host.log').open('w') as log:
        host=subprocess.Popen([str(ROOT.parent/'.toolchains/gsp-sim-1.5.1/sim'),'--bundle',m['bundle'],'--api-json',m['api'],
          '--backend-listen',f'tcp://127.0.0.1:{backend_port}','--backend-required','--api-listen','tcp://127.0.0.1:0',
          '--headless','--fps','1000','--frames','0','--ready-file',str(out/'ready.json')],stdout=log,stderr=subprocess.STDOUT)
    children.append(host);until(lambda:(out/'ready.json').exists(),'host readiness');ready=json.loads((out/'ready.json').read_text())
    with (out/'backend.log').open('w') as log:
        backend=subprocess.Popen([m['executable'],'--endpoint',ready['backend'],'--duration-ms','18000'],
            env=dict(os.environ,ECHOY_TEST_FEEDBACK='1'),stdout=log,stderr=subprocess.STDOUT)
    children.append(backend);until(lambda:'HEALTH_FIXTURE init=0' in (out/'backend.log').read_text(),'C UI readiness')
    api_info=json.loads(Path(m['api']).read_text());assert_v4_geometry(api_info)
    objects={o['name']:o for o in api_info['scenes'][0]['objects']}
    with connect(port=int(ready['api'].rsplit(':',1)[1])) as api:
        def call(method,params=None):
            r=api.call(method,params);assert 'error' not in r,r;return r.get('result')
        def shot(name):
            path=out/(name+'.png');call('screenshot',{'path':str(path),'format':'png'});return Image.open(path).convert('RGB')
        def settle():call('wait',{'frames':5});time.sleep(.08)
        def tap(name):call('tap_object',{'name':name});settle()
        def battery(page,label):
            im=shot('battery-100-'+page);b=objects[label]['bounds'];x,y=b['x1'],b['y1'];w=b['x2']-x;h=b['y2']-y
            crop=im.crop((x,y,x+w,y+h));bg=im.getpixel((390,y+5))
            columns=[xx for xx in range(w) if any(max(abs(v-bg[c]) for c,v in enumerate(crop.getpixel((xx,yy))))>45 for yy in range(h))]
            assert columns and max(columns)>=25,(page,columns)
            assert max(columns)<w-1,(page,'ink reaches clip boundary')
        settle();battery('home','battery_text')
        box=(400,130,464,275);original=shot('slider-original').crop(box)
        call('feed_pointer',{'x':432,'y':200,'pressed':True});call('wait',{'frames':3})
        call('feed_pointer',{'x':432,'y':146,'pressed':True});call('wait',{'frames':3})
        until(lambda:ImageChops.difference(shot('slider-drag').crop(box),original).getbbox() is not None,'drag feedback')
        target=shot('slider-target').crop(box)
        call('feed_pointer',{'x':432,'y':146,'pressed':False});call('wait',{'frames':3})
        for i in range(4):
            current=shot(f'slider-await-{i}').crop(box)
            assert ImageChops.difference(target,current).getbbox() is None,'richness jumped while awaiting owner'
            time.sleep(.07)
        until(lambda:'FEEDBACK_FIXTURE applied=' in (out/'backend.log').read_text(),'owner applied target')
        settle();assert ImageChops.difference(target,shot('slider-applied').crop(box)).getbbox() is None
        tap('role');battery('role','role_battery_text');tap('role_back')
        tap('settings');battery('settings','settings_battery_text')
        for page in ('display','guide','info'):
            tap('settings_'+page);battery(page,page+'_battery_text');tap(page+'_back')
        assert backend.wait(timeout=20)==0;call('quit');assert host.wait(timeout=6)==0
    log=(out/'backend.log').read_text();assert 'ECHOY_UI_ERROR' not in log and 'last_error=0' in log and 'cleanup=0' in log
    result={'passed':['100% visible on all six device pages','real native drag/release holds target through delayed controller snapshots','accepted target remains after acknowledgement','native controls/layout and clean shutdown'],'scope':'official GSP1.5.1 and production C UI; synthetic battery and delayed controller, no physical device'}
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
finally:
    for c in reversed(children):
        if c.poll() is None:
            c.terminate()
            try:c.wait(timeout=6)
            except subprocess.TimeoutExpired:c.kill();c.wait()
