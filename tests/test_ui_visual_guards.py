#!/usr/bin/env python3
"""Destructive image mutations must fail even with perfect touch metadata."""
import json,sys
from pathlib import Path
import numpy as np
from PIL import Image
from ui_image_checks import image_checks
root=Path(__file__).resolve().parents[1]
cases=json.loads((root/'tests/cases/ui-cases.json').read_text())['cases']
captures={c['id']:c for c in json.loads((root/'tests/cases/ui-capture.json').read_text())['cases']}
paper=np.array([241,239,232],dtype=np.uint8)

def run(kind):
    name='role-pulse' if kind=='role' else 'perform-pulse'
    case=next(c for c in cases if c['id']==name and c['locale']=='zh-CN');capture=captures['zh-CN/'+name]
    ref=np.array(Image.open(root/'reference/ui/zh-CN'/(name+'.png')).convert('RGB'))
    assert image_checks(ref,ref,case,capture)['accepted'],'identical image must pass'
    bad=ref.copy()
    if kind=='instrument':bad[154:372,16:358]=paper
    elif kind=='role':bad[119:220,36:444]=paper
    elif kind=='move':
        bad[384:468,204:444]=paper;bad[384:468,212:452]=ref[384:468,204:444]
    elif kind=='opacity':
        # Synthetic faithful paused reference versus its wrong full-bright plot.
        bad[154:372,16:358]=np.clip(np.rint(paper+(ref[154:372,16:358].astype(float)-paper)/.45),0,255).astype(np.uint8)
    else:raise AssertionError(kind)
    result=image_checks(bad,ref,case,capture)
    assert not result['accepted'],f'{kind}: broken drawing incorrectly accepted: {result}'
    print(kind+': correctly rejected')

if __name__=='__main__':
    for kind in sys.argv[1:] or ['instrument','role','move','opacity']:run(kind)
