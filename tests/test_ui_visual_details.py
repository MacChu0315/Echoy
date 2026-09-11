#!/usr/bin/env python3
"""Targeted visual regressions against the original product reference."""
import io,json,subprocess,sys
from pathlib import Path
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[1]

def load(name,old=False):
    if old:
        data=subprocess.check_output(['git','show','5417e5d:output/ui/'+name+'.png'],cwd=root)
        return np.array(Image.open(io.BytesIO(data)).convert('RGB')).astype(int)
    return np.array(Image.open(root/'output/ui'/(name+'.png')).convert('RGB')).astype(int)

def ref(name):return np.array(Image.open(root/'reference/ui'/(name+'.png')).convert('RGB')).astype(int)
def foreground(image,color,rect,tolerance=8):
    x,y,w,h=rect;mask=np.abs(image[y:y+h,x:x+w]-color).max(axis=2)<=tolerance
    ys,xs=np.where(mask);assert len(xs),'expected painted foreground missing'
    return (int(xs.min()+x),int(ys.min()+y),int(xs.max()+x+1),int(ys.max()+y+1)),int(mask.sum())

def checks(old=False):
    outcomes={}
    a=load('zh-CN/perform-waiting',old);r=ref('zh-CN/perform-waiting')
    try:
        _,ac=foreground(a,[139,98,47],(204,400,240,58));_,rc=foreground(r,[139,98,47],(204,400,240,58));outcomes['waiting-color']=ac>=rc*.6
    except AssertionError:outcomes['waiting-color']=False
    a=load('zh-CN/volume',old);r=ref('zh-CN/volume')
    ab,_=foreground(a,[139,149,124],(245,295,65,40),3);rb,_=foreground(r,[139,149,124],(245,295,65,40),3)
    outcomes['volume-percent']=abs(ab[0]-rb[0])<=2
    a=load('zh-CN/role-pulse',old);r=ref('zh-CN/role-pulse')
    _,ac=foreground(a,[217,85,53],(36,204,13,16),20);_,rc=foreground(r,[217,85,53],(36,204,13,16),20)
    outcomes['selected-rounded-bottom']=ac>=rc*.8
    a=load('zh-CN/perform-pulse',old);r=ref('zh-CN/perform-pulse')
    for name,rect in [('role-nav',(36,400,144,58)),('play-nav',(204,400,240,58))]:
        ab,_=foreground(a,[36,38,33],rect);rb,_=foreground(r,[36,38,33],rect)
        outcomes[name]=abs(ab[1]-rb[1])<=2 and abs(ab[3]-rb[3])<=2
    return outcomes

if __name__=='__main__':
    result=checks('--old' in sys.argv);print(json.dumps(result,indent=2));assert all(result.values()),result
