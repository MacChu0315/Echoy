"""Retain exact office-profile PCM from the committed baseline and compare it."""
import argparse, hashlib, json, pathlib, subprocess, tarfile, io
p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sha=subprocess.check_output(['git','rev-parse',a.baseline],cwd=root,text=True).strip()
archive=subprocess.check_output(['git','archive',sha,'projects/echoy/main'],cwd=root)
retained=out/'retained-source';retained.mkdir(exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(archive)) as f:f.extractall(retained,filter='data')
for label,source in [('original',retained),('candidate-original-mode',root)]:
 main=source/'projects/echoy/main'
 names=['band','model','music','arrange','audio','device_pcm']
 if label!='original':names+=['pickup']
 command=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(main),str(root/'tests/pickup/dump_pcm.c')]+[str(main/f'echoy_{n}.c') for n in names]+['-lm','-o',str(out/label)]
 subprocess.run(command,check=True);subprocess.run([str(out/label),str(out/(label+'.pcm'))],check=True)
old=(out/'original.pcm').read_bytes();new=(out/'candidate-original-mode.pcm').read_bytes()
assert len(old)==144*60000*2 and old==new
report=dict(baseline=sha,cases=144,samples=len(old)//2,format='24000 Hz mono signed-16 little endian',identical=True,sha256=hashlib.sha256(old).hexdigest())
(out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
