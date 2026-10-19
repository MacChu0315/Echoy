"""Official native pointer recording, pickup artwork and real shared-C PCM."""
import argparse,json,os,pathlib,re,sys,tempfile,time,wave
from array import array
from contextlib import ExitStack
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from check_desk import DeskCheck,pcm_statistics
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.sim=ROOT.parent/'.toolchains/gsp-sim-1.5.1/sim';a.duration_ms=20000
a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);assert not any(a.output.iterdir())
assert os.environ.get('ECHOY_PHRASE_PICKUP')=='1'
with tempfile.TemporaryDirectory(prefix='echoy-pickup-native-') as temp:
 c=DeskCheck(a,a.output,pathlib.Path(temp))
 try:
  with ExitStack() as stack:
   c.setup(stack);c.record_ready('A');c.tap_at('A',170,420)
   c.until(lambda s:s['tiles']['A']['phase']==2,'Recording did not start',timeout=5)
   time.sleep(.6);c.pad('A');time.sleep(.3);c.pad('A')
   c.until(lambda s:s['tiles']['A']['phase']==0 and s['tiles']['A']['events'] and c.stable(s),'Reply did not finish',timeout=7)
   data=c.export('delayed-input');plan=data['tiles']['A']['current'];source=plan['rootSeed']
   assert 3<=source[0]['step']<=5,source
   assert min(n['step'] for n in plan['full'])>=source[0]['step'],plan
   c.shot('A','pickup-idle-artwork');c.play_ready('A');c.tap('A','transport')
   state=c.until(lambda s:s['tiles']['A']['phase']==5,'Play did not start')
   start=state['tiles']['A']['start_us'];origin=state['audio_origin_us']
   c.until(lambda _:time.monotonic_ns()//1000>=start+100000,'Start not reached',timeout=3)
   c.shot('A','pickup-playing-artwork');time.sleep(2.6)
   c.command('pause_all');c.until(lambda s:s['tiles']['A']['phase']==0,'Pause failed')
   assert c.backend.wait(timeout=20)==0
   c.stop_sampler.set();c.sampler.join(timeout=2)
   for tile in ('A','B','C'):c.call(tile,'quit')
   for child in c.processes[:-1]:assert child.wait(timeout=6)==0
   with wave.open(str(c.wav),'rb') as f:pcm=array('h',f.readframes(f.getnframes()))
   begin=round((start-origin)*.024);end=begin+round(source[0]['step']*3750)-288
   metrics=pcm_statistics(pcm,begin,end);assert metrics['nonzero']>100,metrics
   log=(a.output/'desk.log').read_text();assert re.search(r'ECHOY_DESK_CLOSED error=0 samples=\d+ late_blocks=0\b',log)
   report=dict(first_step=source[0]['step'],leading_pcm=metrics,passed=['native delayed recording unchanged','pickup artwork and grid shown','actual C lead-in PCM audible before first original event','no late blocks; clean owned-process shutdown'])
   (a.output/'checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
 finally:c.close()
