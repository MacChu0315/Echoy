#!/usr/bin/env python3
"""Compare real shared LVGL output against frozen browser pixels. No masks."""
import argparse,json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from ui_image_checks import image_checks
p=argparse.ArgumentParser();p.add_argument('--cases',default='tests/ui_cases.json');p.add_argument('--renderer',default='build-host/echoy_ui_runner');a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'output/ui';out.mkdir(parents=True,exist_ok=True)
cases=json.loads((root/a.cases).read_text())['cases'];captures={c['id']:c for c in json.loads((root/'tests/cases/ui-capture.json').read_text())['cases']};results=[]
for c in cases:
 key=c['locale']+'/'+c['id'];dest=out/key;dest.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run([str(root/a.renderer),key,str(dest.with_suffix('.ppm')),str(dest.with_suffix('.hits.bin'))],check=True)
 grid=np.fromfile(dest.with_suffix('.hits.bin'),dtype=np.uint8).reshape((480,480));capture=captures[key]
 action_values={'choose':1,'back':2,'play':3,'volume':6,'dismiss-volume':7,'cancel-load':12,'recover':11}
 boundaries=0
 for action in capture['actions']:
  name=action['action']
  if c['state']=='volume' and name not in ['back','dismiss-volume']:continue
  if name=='dismiss-volume':continue # The dialog intentionally occludes the scrim's rectangle.
  expected=100+['pulse','voice','dust','harmony'].index(action['value']) if name=='role' else 110+int(action['value']) if name=='phrase' else 7 if c['state']=='volume' and name=='back' else action_values[name]
  r=action['rect'];x,y,w,h=(round(r[k]) for k in ['x','y','width','height'])
  actual_region=grid[y:y+h,x:x+w];assert np.all(actual_region==expected),(key,name,'touch rectangle differs',np.unique(actual_region))
  ys,xs=np.where(grid==expected);bounds=(int(xs.min()),int(ys.min()),int(xs.max()+1),int(ys.max()+1));target=(x,y,x+w,y+h)
  if c['state']!='volume':assert max(abs(a-b) for a,b in zip(bounds,target))<=2,(key,name,bounds,target)
  boundaries+=1
 actual=Image.open(dest.with_suffix('.ppm')).convert('RGB');assert actual.size==(480,480),key
 actual.save(dest.with_suffix('.png'));ref=Image.open(root/'reference/ui'/c['locale']/(c['id']+'.png')).convert('RGB')
 x=np.asarray(actual,dtype=np.int16);y=np.asarray(ref,dtype=np.int16)
 # RGB565 color quantization allowance is measured separately, never a page mask.
 delta=np.abs(x-y);rgb565=np.array([7,3,7]);beyond=np.maximum(delta-rgb565,0)
 diff=Image.fromarray(np.minimum(delta*4,255).astype(np.uint8));diff.save(dest.with_name(dest.name+'-diff').with_suffix('.png'))
 regional=image_checks(x,y,c,capture)
 results.append({'accepted':regional['accepted'],'regions':regional['regions'],'id':key,'mae':float(delta.mean()),'rgb565AdjustedMae':float(beyond.mean()),'fractionOver24':float((delta.max(axis=2)>24).mean()),'max':int(delta.max()),'masks':[],'verifiedControlBoundaries':boundaries})
(out/'metrics.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps({'cases':len(results),'meanMAE':sum(r['mae'] for r in results)/len(results),'worstMAE':max(r['mae'] for r in results),'masks':[]},indent=2))
# A visible mismatch is a failure; baseline never rewritten by this tool.
failed=[{'id':r['id'],'checks':[c for c in r['regions'] if not c['accepted']]} for r in results if not r['accepted']]
if failed:print(json.dumps(failed,indent=2))
from test_ui_visual_guards import run
for mutation in ['instrument','role','move','opacity']:run(mutation)
from test_ui_visual_details import checks
details=checks();print(json.dumps({'visualDetails':details},indent=2))
raise SystemExit(bool(failed) or not all(details.values()))
