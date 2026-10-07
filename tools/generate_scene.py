#!/usr/bin/env python3
"""Generate the native GSP scene from the frozen prototype's measured geometry."""
from pathlib import Path
import hashlib
import json
import re
import shutil
from fontTools.ttLib import TTFont
from fontTools import subset
from PIL import Image
import cairosvg
ROOT=Path(__file__).resolve().parents[1]
UI=ROOT/'projects/echoy/ui'
SOURCE=ROOT/'reference/echoy-arrange-B-v3.html'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()=='241a0a58e0e33cb906b4bf637c2d55f219de988df2a683c68d68f767c7dee6b5'
text=SOURCE.read_text()
chars=''.join(sorted(set(text)))
(UI/'fonts').mkdir(exist_ok=True)
(UI/'icons').mkdir(exist_ok=True)
fonts=[]
manifest_path=UI/'fonts/manifest.json'
pinned_fonts={entry['file']:entry for entry in json.loads(manifest_path.read_text())} if manifest_path.exists() else {}
def check_font_input(source,dest):
    expected=pinned_fonts.get(dest.name)
    if expected:
        if hashlib.sha256(source.read_bytes()).hexdigest()!=expected['source_sha256']:
            raise SystemExit('Font source differs from the recorded baseline: '+source.name)
        if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest()!=expected['sha256']:
            raise SystemExit('Frozen font changed: '+dest.name)
for weight in ['Regular','Medium','Bold']:
    source=Path('/usr/share/fonts/opentype/noto/NotoSansCJK-'+weight+'.ttc')
    dest=UI/'fonts'/('NotoSansSC-'+weight+'.otf')
    check_font_input(source,dest)
    if not dest.exists():
        font=TTFont(source,fontNumber=2,recalcTimestamp=False)
        options=subset.Options(); options.recalc_timestamp=False
        sub=subset.Subsetter(options=options); sub.populate(text=chars); sub.subset(font); font.save(dest)
    fonts.append({'source':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'file':dest.name,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
for source in [Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'),Path('/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf')]:
    dest=UI/'fonts'/source.name;check_font_input(source,dest);shutil.copyfile(source,dest)
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    fonts.append({'source':source.name,'source_sha256':digest,'file':dest.name,'sha256':digest})
for name in ['fonts-noto-cjk','fonts-dejavu-core','fonts-liberation']:
    license_text=Path('/usr/share/doc/'+name+'/copyright').read_text()
    (UI/'fonts'/(name+'-LICENSE.txt')).write_text('\n'.join(line.rstrip() for line in license_text.splitlines())+'\n')
manifest_path.write_text(json.dumps(fonts,indent=2)+'\n')
icon_line=next(x for x in text.splitlines() if x.startswith('const ICON='))
icons=dict(re.findall(r"(\w+):'([^']*)'",icon_line))
icons['pause']='<rect x="6.5" y="5" width="4" height="14" rx=".8" fill="currentColor" stroke="none"/><rect x="13.5" y="5" width="4" height="14" rx=".8" fill="currentColor" stroke="none"/>'
for name,path in icons.items():
    color='#fafbf3' if name in ('play','pause') else '#b26950' if name in ('record','cross') else '#5f7450'
    svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><g id="icon">{path.replace("currentColor",color)}</g></svg>'
    (UI/'icons'/(name+'.svg')).write_text(svg)
Image.new('RGB',(300,204),(241,239,232)).save(UI/'canvas.png')
Image.new('RGB',(300,15),(241,239,232)).save(UI/'feedback.png')
(UI/'icons/pad-border.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="300" height="204" viewBox="0 0 300 204"><g id="icon"><rect x=".5" y=".5" width="299" height="203" rx="29.5" fill="none" stroke="#ccd4c1" stroke-width="1" stroke-dasharray="3 3"/></g></svg>')
# GSPC 0.6.1 does not preserve SVG stroke-dasharray. Rasterize this small
# static decoration; controls, text, music art and feedback remain native.
cairosvg.svg2png(url=str(UI/'icons/pad-border.svg'),write_to=str(UI/'icons/pad-border.png'))
objects=[]
def add(kind,name,x,y,w,h,parent=-1,**kw):
    obj=dict(type=kind,parent=parent,name=name,x=x,y=y,w=w,h=h,**kw); objects.append(obj); return len(objects)-1
charset=''.join(sorted(set(chars.replace('⠿','')+'预备录入这段留在这里了准备新的声音正在演奏已取消原节奏保留没有录入先听这一次点「录一段」才记录节奏')))
def label(name,x,y,w,h,content,size,color,parent,**kw):
    return add('label',name,x,y,w,h,parent,text=content,font_size=size,fg_color=color,**kw)
def dyn(name,x,y,w,h,content,size,color,parent,**kw):
    return label(name,x,y,w,h,content,size,color,parent,bind=name,bind_target='text',font_charset=('0123456789%' if name=='battery_text' else charset),**kw)
paper=add('container','paper',0,0,480,480,bg_color='#f1efe8',radius=60,clip_children=True)
home=add('container','home',0,0,480,480,paper)
# Browser text ink is two pixels above GSP's default centered Noto line box.
# Separate native labels preserve the prototype's one-pixel CJK title tracking.
dyn('role_title_first',35,23,26,31,'脉',25,'#d95535',home,font='fonts/NotoSansSC-Bold.otf')
dyn('role_title_second',61,23,26,31,'冲',25,'#d95535',home,font='fonts/NotoSansSC-Bold.otf')
dyn('phrase_title',35,55,240,24,'重音',17,'#78836d',home)
# Header button preserves the full 132x84 input area and its inset surface.
rec=add('button','record',244,0,132,84,home,callback='record_click',enabled=True,bg_color='#00000000')
add('container','record_surface',3,20,126,52,rec,bg_color='#e5e9de',border_color='#d0d8c6',border_width=1,radius=16)
add('image','record_icon',31,38,15,15,rec,image='icons/record.svg',svg_layout='canvas',svg_element='icon',fit='stretch')
add('image','cancel_icon',31,38,15,15,rec,image='icons/cross.svg',svg_layout='canvas',svg_element='icon',fit='stretch',hidden=True)
dyn('record_text',53,30,60,32,'录一段',16,'#667256',rec,hidden=True)
label('cancel_text',53,30,60,32,'取消',16,'#956244',rec,hidden=True)
# Battery is a placeholder supplied by the PC adapter until hardware integration.
add('container','battery_outline',395,41,19,12,home,border_color='#78836c',border_width=1,radius=2)
add('container','battery_tip',416,45,1,4,home,bg_color='#78836c')
add('container','battery_fill',398,44,9,6,home,bg_color='#78836c')
dyn('battery_text',424,35,23,22,'72%',12,'#78836c',home,font='fonts/DejaVuSansMono.ttf')
dyn('status',90,86,300,24,'先试着敲几下',14,'#657457',home,text_align='center',hidden=True)
label('capture_status',90,86,300,24,'先试着敲几下',14,'#d95535',home,text_align='center',hidden=True,bind='capture_status',bind_target='text',font_charset=charset)
for name,x,color,default,top,bottom in [('volume',16,'#657756',65,'','音量'),('richness',400,'#d95535',50,'丰富','简洁')]:
    p=add('container',name, x,113,64,207,home,bg_color='#e7ebdf',border_color='#d8dece',border_width=1,radius=24)
    if top:label(name+'_top',0,13,64,24,top,14,'#67785a',p,text_align='center')
    else:add('image','speaker',21,14,22,22,p,image='icons/speaker.svg',svg_layout='canvas',svg_element='icon',fit='stretch')
    label(name+'_bottom',0,169,64,22,bottom,14,'#67785a',p,text_align='center')
    rail=add('container',name+'_rail',30,49,4,111,p,bg_color='#c3cdb6',radius=2,clip_children=True)
    height=round(default*111/100)
    add('container',name+'_fill',0,111-height,4,111,rail,bg_color=color,radius=2)
    # The native pointer observer preserves relative drag (180 px), not jump-to-value.
    kw=dict(bg_color='#fafbf5',border_color='#93a382' if name=='volume' else '#b87943',border_width=1,radius=13 if name=='volume' else 10)
    knob=add('container',name+'_knob',20 if name=='volume' else 17,round(160-height-12),25 if name=='volume' else 30,25 if name=='volume' else 27,p,**kw)
    if name=='richness':label('equal',0,0,30,27,'=',21,'#687b57',knob,text_align='center',font='fonts/LiberationSans-Regular.ttf')
add('image','art',90,113,300,204,home,image='canvas.png',codec='lossless',bind='art',bind_target='resource')
add('image','pad_border',90,113,300,204,home,image='icons/pad-border.png',codec='lossless',hidden=True)
dyn('touch_hint',90,289,300,24,'试奏区',13,'#6b795c',home,text_align='center')
add('container','progress_bg',103,318,274,2,home,bg_color='#dce0d2',hidden=True)
add('progress','progress',103,318,274,2,home,min=0,max=100,value=0,fg_color='#d95535',bg_color='#dce0d2',hidden=True)
add('image','feedback',90,329,300,15,home,image='feedback.png',codec='lossless',bind='feedback',bind_target='resource')
dyn('hint',28,348,424,23,'点「录一段」才记录节奏',12,'#7d896e',home,text_align='center')
for name,x,word,icon in [('role',28,'角色','wave'),('settings',312,'设置','gear')]:
    p=add('button',name,x,381,140,84,home,callback=name+'_click',enabled=True,bg_color='#e7ecdf',border_color='#cfd8c3',border_width=1,radius=21)
    add('image',name+'_icon',38,30,23,23,p,image='icons/'+icon+'.svg',svg_layout='canvas',svg_element='icon',fit='stretch')
    label(name+'_label',68,26,45,28,word,17,'#4c613e',p,font='fonts/NotoSansSC-Medium.otf')
play=add('button','transport',198,381,84,84,home,callback='transport_click',enabled=False,bg_color='#596d4b',radius=42)
add('image','play_icon',25,25,34,34,play,image='icons/play.svg',svg_layout='canvas',svg_element='icon',fit='stretch')
add('image','pause_icon',25,25,34,34,play,image='icons/pause.svg',svg_layout='canvas',svg_element='icon',fit='stretch',hidden=True)
for obj in objects:
    if obj['type']=='button':
        obj['disabled_color']='#f1efe8'; obj['disabled_opacity']=145
    if obj['name'].endswith(('_knob','_fill')) and not obj['name'].startswith('battery'):
        y=obj['y']; obj['y']={'default':y,'min':0,'max':180}
    if obj['name'] in ('record_surface','transport') or obj['name'].startswith('step_'):
        obj['bind']=obj['name']+'_color'; obj['bind_target']='color'
    if obj['name'] in ('record_icon','play_icon'):
        obj['hidden']=True
scene=dict(screen='echoy',w=480,h=480,screen_bg='#363b30',font='fonts/NotoSansSC-Regular.otf',default_font_size=16,swipe=False,objects=objects)
(UI/'main.json').write_text(json.dumps(scene,ensure_ascii=False,indent=2)+'\n')
print('Generated native scene:',len(objects),'objects')
