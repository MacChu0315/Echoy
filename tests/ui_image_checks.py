"""Independent image-region/structure/boundary gates; no excluded pixels.

Structural masks below measure foreground coverage. They do not mask pixels
out of either regional or whole-image absolute-error metrics.
"""
import re
import numpy as np
from PIL import Image,ImageFilter
PAPER=np.array([241,239,232],dtype=np.int16)

def dilate(mask,radius=2):
    return np.asarray(Image.fromarray(mask.astype(np.uint8)*255).filter(ImageFilter.MaxFilter(radius*2+1)))>0

def bounds(mask,origin):
    ys,xs=np.where(mask)
    if not len(xs):return None
    return [int(xs.min())+origin[0],int(ys.min())+origin[1],int(xs.max())+origin[0]+1,int(ys.max())+origin[1]+1]

def image_checks(actual,reference,case,capture):
    a=actual.astype(np.int16);r=reference.astype(np.int16);delta=np.abs(a-r)
    checks=[];regions=[]
    if capture['route']=='perform':
        regions.append(('instrument',(16,154,342,30 if case['state']=='volume' else 218)))
        if case['state']!='volume':regions.append(('expression',(360,154,96,218)))
    if capture['route']=='role':regions.extend([('role-cards',(36,119,408,101)),('phrase-cards',(36,276,408,98))])
    for name,(x,y,w,h) in regions:
        aa=a[y:y+h,x:x+w];rr=r[y:y+h,x:x+w]
        background=np.median(rr.reshape(-1,3),axis=0) if case['state']=='volume' else PAPER
        actual_background=np.median(aa.reshape(-1,3),axis=0) if case['state']=='volume' else PAPER
        ai=np.abs(aa-actual_background).max(axis=2);ri=np.abs(rr-background).max(axis=2)
        ref_foreground=ri>5;actual_foreground=ai>5
        coverage=float((dilate(actual_foreground)&ref_foreground).sum()/max(1,ref_foreground.sum()))
        ink_ratio=float(ai.sum()/max(1,ri.sum()))
        mae=float(np.abs(aa-rr).mean())
        accepted=coverage>=.65 and .50<=ink_ratio<=1.65 and mae<=12
        checks.append({'name':name,'mae':mae,'foregroundCoverageWithin2px':coverage,'inkRatio':ink_ratio,'accepted':accepted})
    # A drawing cannot claim correct boundaries merely because its hit box is right.
    # Measure filled pixels of every opaque critical component independently.
    selectors=['.role-tab','.phrase-option','.status-symbol','.status-action','.device-button.transport-button','.volume-sheet']
    for selector in selectors:
        if case['state']=='volume' and selector!='.volume-sheet':continue
        for index,style in enumerate(capture['styles'].get(selector,[])):
            rgb=re.fullmatch(r'rgb\((\d+), (\d+), (\d+)\)',style['backgroundColor'])
            if not rgb:continue
            color=np.array([int(v) for v in rgb.groups()]);rect=style['rect'];x,y,w,h=(round(rect[k]) for k in ['x','y','width','height'])
            x0=max(0,x-3);y0=max(0,y-3);x1=min(480,x+w+3);y1=min(480,y+h+3)
            rb=bounds(np.abs(r[y0:y1,x0:x1]-color).max(axis=2)<=2,(x0,y0))
            ab=bounds(np.abs(a[y0:y1,x0:x1]-color).max(axis=2)<=2,(x0,y0))
            error=max(abs(v-u) for v,u in zip(ab,rb)) if ab and rb else None
            checks.append({'name':f'{selector}[{index}]','referenceDrawBounds':rb,'actualDrawBounds':ab,'boundaryError':error,'accepted':error is not None and error<=2})
    if capture['route']=='perform' and case['state']!='volume':
        # Every one of the 16 actual score dots must be painted near its reference.
        for step in range(16):
            x=45+19*step;x0=x-6;y0=358;rr=r[y0:371,x0:x+7];aa=a[y0:371,x0:x+7]
            rb=bounds(np.abs(rr-PAPER).max(axis=2)>5,(x0,y0));ab=bounds(np.abs(aa-PAPER).max(axis=2)>5,(x0,y0))
            error=max(abs(v-u) for v,u in zip(ab,rb)) if ab and rb else None
            checks.append({'name':f'score[{step}]','referenceDrawBounds':rb,'actualDrawBounds':ab,'boundaryError':error,'accepted':error is not None and error<=2})
    accepted=bool(delta.mean()<=8 and (delta.max(axis=2)>24).mean()<=.09 and all(c['accepted'] for c in checks))
    return {'accepted':accepted,'regions':checks}
