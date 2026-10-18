#!/usr/bin/env python3
"""Generate the native GSP scene from the frozen prototype's measured geometry."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import math
from fontTools.ttLib import TTFont
from fontTools import subset
from PIL import Image, ImageDraw, ImageFont
from io import BytesIO
import cairosvg
ROOT=Path(__file__).resolve().parents[1]
UI=ROOT/'projects/echoy/ui'
VERSION=re.search(r'project\(echoy VERSION ([0-9.]+)\)',
                  (ROOT/'projects/echoy/CMakeLists.txt').read_text()).group(1)
SOURCE=ROOT/'reference/v4/echoy-arrange-B-v4.html'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()=='c7e3e2e6992dc1f21857bfa6a8dc1357b5f95ea2dfad0425f60e83b1085ee7e3'
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
    if not dest.exists() or weight in ('Regular', 'Medium'):
        font=TTFont(source,fontNumber=2,recalcTimestamp=False)
        options=subset.Options(); options.recalc_timestamp=False
        sub=subset.Subsetter(options=options); sub.populate(text=chars+Path(__file__).read_text()); sub.subset(font); font.save(dest)
    fonts.append({'source':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'file':dest.name,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
for source in [Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'),Path('/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf'),Path('/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf')]:
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
icons['display']='<rect x="3" y="4" width="18" height="14" rx="3"/><path d="M9 21h6M12 18v3"/><circle cx="12" cy="11" r="3"/>'
icons['info']='<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.01"/>'
for name,path in icons.items():
    color='#fafbf3' if name in ('play','pause') else '#b26950' if name in ('record','cross') else '#5f7450'
    svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><g id="icon">{path.replace("currentColor",color)}</g></svg>'
    (UI/'icons'/(name+'.svg')).write_text(svg)
Image.new('RGB',(300,204),(241,239,232)).save(UI/'canvas.png')
Image.new('RGB',(300,15),(241,239,232)).save(UI/'feedback.png')
Image.new('RGB',(206,101),(229,231,222)).save(UI/'role-main.png')
Image.new('RGB',(73,48),(236,239,228)).save(UI/'role-neighbor.png')
(UI/'icons/pad-border.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="300" height="204" viewBox="0 0 300 204"><g id="icon"><rect x=".5" y=".5" width="299" height="203" rx="29.5" fill="none" stroke="#ccd4c1" stroke-width="1" stroke-dasharray="3 3"/></g></svg>')
# GSP's clip_children is rectangular. These four small vector cutouts keep
# the strip's R23 outside corners paper-colored as its native tints move.
(UI/'icons/strip-corners.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="244" height="84" viewBox="0 0 244 84"><g id="icon" fill="#f1efe8"><path d="M0 0H23A23 23 0 0 0 0 23Z M221 0H244V23A23 23 0 0 0 221 0Z M244 61V84H221A23 23 0 0 0 244 61Z M23 84H0V61A23 23 0 0 0 23 84Z"/></g></svg>')
(UI/'icons/strip-border.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="244" height="84" viewBox="0 0 244 84"><g id="icon"><rect x=".5" y=".5" width="243" height="83" rx="22.5" fill="none" stroke="#d2d9c9" stroke-width="1"/></g></svg>')
# GSPC 0.6.1 does not preserve SVG stroke-dasharray. Rasterize this small
# static decoration; controls, text, music art and feedback remain native.
cairosvg.svg2png(url=str(UI/'icons/pad-border.svg'),write_to=str(UI/'icons/pad-border.png'))
objects=[]
def add(kind,name,x,y,w,h,parent=-1,**kw):
    obj=dict(type=kind,parent=parent,name=name,x=x,y=y,w=w,h=h,**kw); objects.append(obj); return len(objects)-1
# Runtime glyphs are scoped to the actual UI text, never HTML/CSS/JavaScript.
# GSP embeds one font pack per (font,size); an all-source corpus at every size
# makes even the old home-only scene almost fill the 1 MiB UI partition.
home_status_zh='中央试奏 · 录制才保存准备录制预备-1234录入 / 准备新的声音下一小节生效等共同拍点试听试听一次 · 结束自动停等共同拍点开始已取消，原节奏保留没有录入，原节奏保留新声音没有准备好，原来的继续已连接等待左侧录制乐句录完后加入结束后加入跟随左侧连接恢复中'
home_status_en='Tap to try · Record to keep Preparing to record Ready · 1234 Recording / Preparing a new sound Changes at the next bar Audition at the shared beat One reply · Stops at the end Waiting for the shared beat Cancelled; original rhythm kept No notes captured; original kept Preparation failed; current sound continues Connected Waiting for left phrase recording Join after recording Join when done Following left Reconnecting'
home_status_zh+='麦克风采集中 · 点暂停停止声音测量'
home_status_en+=' Microphone active · Pause to stop Sound measurement'
role_names_zh='脉冲声线微尘和声'
phrase_names_zh='重音切分留白短句轻答长线颗粒点缀余韵短和弦分解接句'
phrase_names_en='Accent Syncopation Space Short phrase Light reply Long line Grains Touches Trails Short chords Arpeggio Answer'
home_charsets={'role_title_first':role_names_zh,'role_title_second':role_names_zh,'phrase_title':phrase_names_zh,'battery_text':'0123456789%-','record_text':'录制','status':home_status_zh,'capture_status':home_status_zh,'touch_hint':'试奏区'}
charset=home_status_zh
def label(name,x,y,w,h,content,size,color,parent,**kw):
    return add('label',name,x,y,w,h,parent,text=content,font_size=size,fg_color=color,**kw)
def dyn(name,x,y,w,h,content,size,color,parent,**kw):
    corpus=kw.pop('font_charset',home_charsets.get(name,content))
    return label(name,x,y,w,h,content,size,color,parent,bind=name,bind_target='text',font_charset=corpus,**kw)
paper=add('container','paper',0,0,480,480,bg_color='#f1efe8',radius=60,clip_children=True)
home=add('layer','home',0,0,480,480,paper,bind='home_visible',bind_target='visible')
# Browser text ink is two pixels above GSP's default centered Noto line box.
# v4 keeps role and expression on one line, with .2px CJK tracking.
dyn('role_title_first',34,25,26,32,'脉',25,'#d95535',home,font='fonts/NotoSansSC-Bold.otf')
dyn('role_title_second',60,25,26,32,'冲',25,'#d95535',home,font='fonts/NotoSansSC-Bold.otf')
label('identity_separator',94,26,8,32,'·',17,'#74806b',home,font='fonts/LiberationSans-Regular.ttf')
dyn('phrase_title',110,25,220,32,'重音',18,'#78836d',home)
# Hardware health can be unknown: expose fill visibility as well as percentage text.
add('container','battery_outline',399,37,19,12,home,border_color='#78836c',border_width=1,radius=2)
add('container','battery_tip',420,41,1,4,home,bg_color='#78836c')
add('container','battery_fill',402,40,9,6,home,bg_color='#78836c',bind='battery_fill_visible',bind_target='visible')
dyn('battery_text',428,31,23,22,'72%',12,'#78836c',home,font='fonts/DejaVuSansMono.ttf')
dyn('status',24,339,432,23,'中央试奏 · 录制才保存',13,'#7c886e',home,text_align='center',hidden=True)
label('capture_status',24,339,432,23,'预备-3',13,'#a46848',home,text_align='center',hidden=True,bind='capture_status',bind_target='text',font_charset=charset)
for name,x,color,default,top,bottom in [('volume',16,'#657756',65,'','音量'),('richness',400,'#d95535',50,'丰富','简洁')]:
    p=add('container',name, x,89,64,207,home,bg_color='#e7ebdf',border_color='#d8dece',border_width=1,radius=24)
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
home_art=add('layer','home_art',0,0,480,480,home,bind='home_art_visible',bind_target='visible')
add('image','art',90,89,300,204,home_art,image='canvas.png',codec='lossless',bind='art',bind_target='resource')
add('image','pad_border',90,89,300,204,home,image='icons/pad-border.png',codec='lossless',hidden=True)
dyn('touch_hint',90,265,300,24,'试奏区',13,'#6b795c',home,text_align='center')
add('container','progress_bg',103,301,274,2,home,bg_color='#dce0d2',hidden=True)
add('progress','progress',103,301,274,2,home,min=0,max=100,value=0,fg_color='#d95535',bg_color='#dce0d2',hidden=True)
add('image','feedback',90,315,300,15,home,image='feedback.png',codec='lossless',bind='feedback',bind_target='resource')
for name,x,word,asset in [('role',22,'角色','wave'),('settings',374,'设置','gear')]:
    p=add('button',name,x,378,84,84,home,callback=name+'_click',enabled=True,bg_color='#e9ede2',border_color='#cfd8c3',border_width=1,radius=22)
    add('image',name+'_icon',13,32,20,20,p,image='icons/'+asset+'.svg',svg_layout='canvas',svg_element='icon',fit='stretch',tint='#536746')
    label(name+'_label',39,407-378,39,22,word,16,'#536746',p,font='fonts/NotoSansSC-Medium.otf')

# Background and hit geometry are independent from the changing glyphs.
strip=add('container','strip',118,378,244,84,home,bg_color='#e7ecdf',radius=23,clip_children=True)
for name,x,color,low,high in [('record',-122,'#eee8df',-244,0),('play',122,'#e8ede1',0,244)]:
    # A fixed-width fill translated through the clip gives exactly the same
    # two complementary visible widths as CSS. GSP 1.5.1 rejects a COLOR bind
    # combined with dynamic w/opacity; the controller preblends tint alpha into
    # the strip's fixed background and updates this supported RGB888 color.
    add('layer','strip_'+name+'_tint',{'default':x,'min':low,'max':high},0,244,84,strip,bg_color=color,bind='strip_'+name+'_tint_color',bind_target='color')
add('container','strip_divider',122,22,1,40,strip,bg_color='#d4d7c9',opacity={'default':255,'min':0,'max':255})
rec=add('button','record',0,0,122,84,strip,callback='record_click',enabled=True,bg_color='#00000000',bind='record_visible',bind_target='visible')
play=add('button','transport',122,0,122,84,strip,callback='transport_click',enabled=False,bg_color='#00000000',bind='transport_visible',bind_target='visible')
add('button','transport_only',0,0,244,84,strip,callback='transport_click',enabled=False,hidden=True,bg_color='#00000000',bind='transport_only_visible',bind_target='visible')
empty=add('button','record_empty',0,0,244,84,strip,callback='record_click',enabled=False,hidden=True,bg_color='#00000000',bind='record_empty_visible',bind_target='visible')
single=add('button','strip_action',0,0,244,84,strip,callback='strip_action_click',enabled=False,hidden=True,bg_color='#00000000',bind='strip_action_visible',bind_target='visible')

# Page and locale layers are explicit visibility targets. A name alone does
# not expose visibility, and each bind must be unique in GSPC 0.6.1.
PAPER='#f1efe8'
REGULAR='fonts/NotoSansSC-Regular.otf'
MEDIUM='fonts/NotoSansSC-Medium.otf'
LATIN='fonts/LiberationSans-Regular.ttf'
LATIN_BOLD='fonts/LiberationSans-Bold.ttf'
ACCENTS={'pulse':'#d95535','voice':'#496da1','dust':'#7b8452','harmony':'#aa7b3d'}
ROLES={
    'pulse':('脉冲','Pulse',['重音','切分','留白'],['Accent','Syncopation','Space'],'用鼓点的不同附和方式，把你的节奏衬托起来。','Layered drum support around your rhythm.'),
    'voice':('声线','Voice',['短句','轻答','长线'],['Short phrase','Light reply','Long line'],'用旋律接住你的敲击。','Your rhythm, given a melodic voice.'),
    'dust':('微尘','Dust',['颗粒','点缀','余韵'],['Grains','Touches','Trails'],'让敲击生出颗粒与余韵。','Small grains and trails around your taps.'),
    'harmony':('和声','Harmony',['短和弦','分解','接句'],['Short chords','Arpeggio','Answer'],'以短奏、分解和小句附和。','Short chords, broken notes and replies.'),
}
contract={'routes':{},'locales':{},'scroll':{},'role_copy':{},'home_titles':{},'callbacks':[]}

def layer(name,parent=paper,x=0,y=0,w=480,h=480,hidden=True,**kw):
    return add('layer',name,x,y,w,h,parent,hidden=hidden,bind=name+'_visible',bind_target='visible',**kw)

def route(name):
    parent=layer(name+'_page')
    contract['routes'][name]=name+'_page'
    return parent

def locales(name,parent,w=480,h=480):
    ids={lang:layer(name+'_'+lang,parent,w=w,h=h,hidden=lang=='en') for lang in ('zh','en')}
    contract['locales'][name]={lang:objects[idx]['name'] for lang,idx in ids.items()}
    return ids

def text_pair(name,x,y,w,h,zh,en,size,color,parents,en_size=None,weight=False,dynamic=True,**kw):
    for lang,content in [('zh',zh),('en',en)]:
        suffix='' if lang=='zh' else '_en'
        options=dict(kw)
        if lang=='zh' and options.get('overflow')=='ellipsis':options['overflow']='clip'
        options['font']=MEDIUM if weight and lang=='zh' else LATIN_BOLD if weight else REGULAR
        if lang=='en' and not weight:options['font']=LATIN
        if dynamic:options.update(bind=name+suffix,bind_target='text',font_charset=content)
        label(name+suffix,x,y+(2 if lang=='en' and 'header' in name else 0),w,h,content,(en_size or size) if lang=='en' else size,color,parents[lang],**options)

def icon(name,asset,x,y,w,h,parent,tint=None,**kw):
    if tint:kw['tint']=tint
    return add('image',name,x,y,w,h,parent,image='icons/'+asset+'.svg',svg_layout='canvas',svg_element='icon',fit='stretch',**kw)

def button(name,x,y,w,h,parent,callback,**kw):
    return add('button',name,x,y,w,h,parent,callback=callback,enabled=True,**kw)

def header(name,parent,zh,en):
    back=button(name+'_back',18,12,84,84,parent,'back_click',bg_color='#00000000',radius=22)
    # The frozen HTML's negative-z-index ::before is behind the page; its
    # visible back control is only this arrow, not a filled inset tile.
    icon(name+'_back_icon','back',29,29,25,25,back,tint='#5d6c50')
    add('container',name+'_battery_outline',407,42,19,12,parent,border_color='#78836c',border_width=1,radius=2)
    add('container',name+'_battery_tip',428,46,1,4,parent,bg_color='#78836c')
    add('container',name+'_battery_fill',410,45,9,6,parent,bg_color='#78836c',bind=name+'_battery_fill_visible',bind_target='visible')
    dyn(name+'_battery_text',435,35,25,22,'72%',11,'#78836c',parent,font='fonts/DejaVuSansMono.ttf',font_charset='0123456789%-')
    return (zh,en)

def scroll_area(name,parent,w,h,content_height,x,y):
    viewport=add('container',name+'_viewport',x,y,w,h,parent,clip_children=True)
    content=add('container',name+'_content',0,{'default':0,'min':-1600,'max':0},w,content_height,viewport)
    contract['scroll'][name]={'region':[x,y,x+w,y+h],'content':name+'_content','viewport_height':h,'height':{}}
    return content

# The home geometry and its controller entry points stay intact. Latin text
# uses the HTML's Arial-compatible font; CJK retains the pinned Noto subset.
home_text=locales('home',home)
contract['routes']['home']='home'
for obj in objects[:home_text['zh']]:
    if obj['type']!='label' or obj['name'] in ('battery_text','equal'):continue
    p=obj['parent'];x=obj['x'];y=obj['y']
    while p!=home:
        x+=objects[p]['x'];y+=objects[p]['y'];p=objects[p]['parent']
    obj.update(parent=home_text['zh'],x=x,y=y)
home_en={
    'identity_separator':('·',17,'·'),
    'phrase_title':('Accent',16,phrase_names_en),
    'status':('Tap to try · Record to keep',12,home_status_en),
    'capture_status':('Ready · 3',12,home_status_en),
    'volume_bottom':('Volume',14,'Volume'),
    'richness_top':('Rich',14,'Rich'),
    'richness_bottom':('Simple',14,'Simple'),
    'touch_hint':('Try here',13,'Try here'),
    'role_label':('Role',12,'Role'),
    'settings_label':('Settings',12,'Settings'),
}
for obj in list(objects):
    if obj['name'] not in home_en:continue
    content,size,corpus=home_en[obj['name']]
    clone=dict(obj);clone.update(name=obj['name']+'_en',parent=home_text['en'],text=content,font_size=size,font=LATIN,font_charset=corpus,bind=obj['name']+'_en',bind_target='text')
    if obj['name'] in ('role_label','settings_label'):
        clone.update(w=48,font=LATIN)
        clone['x']=62 if obj['name']=='role_label' else 405
        clone['y']=409
    if obj['name']=='phrase_title':clone.update(x={'default':117,'min':107,'max':155},y=26)
    if obj['name']=='identity_separator':clone.update(x={'default':103,'min':93,'max':142},y=26)
    if obj['name'] in ('status','capture_status'):clone['y']=340
    objects.append(clone)
    if obj['name'] in ('cancel_text','volume_bottom','richness_top','richness_bottom','role_label','settings_label'):
        obj.update(bind=obj['name'],bind_target='text',font_charset=obj['text'])
for name,x,english_x in [('role_icon',35,41),('settings_icon',387,384)]:
    original=next(obj for obj in objects if obj['name']==name)
    original.update(parent=home_text['zh'],x=x,y=410)
    clone=dict(original);clone.update(name=name+'_en',parent=home_text['en'],x=english_x,y=412,w=17,h=17);objects.append(clone)
# Keep all footer glyphs under their real button so its disabled overlay also
# dims the icon and wording, rather than only washing out the background.
for name in ('role','settings'):
    button_index=next(i for i,obj in enumerate(objects) if obj['name']==name)
    button_obj=objects[button_index]
    footer_text=locales(name+'_footer',button_index,w=84,h=84)
    for lang,suffix in [('zh',''),('en','_en')]:
        for kind in ('icon','label'):
            obj=next(obj for obj in objects if obj['name']==name+'_'+kind+suffix)
            obj.update(parent=footer_text[lang],x=obj['x']-button_obj['x'],y=obj['y']-button_obj['y'])
# A label has one binding family; known role titles use separately colored
# targets. This preserves real role colors without unsupported dual bindings.
for role,data in ROLES.items():
    zh_group=layer('home_title_'+role,home_text['zh'],hidden=role!='pulse')
    en_group=layer('home_title_'+role+'_en',home_text['en'],hidden=role!='pulse')
    contract['home_titles'][role]={'zh':'home_title_'+role,'en':'home_title_'+role+'_en'}
    for i,character in enumerate(data[0]):
        name='role_title_'+('first' if i==0 else 'second')
        if role=='pulse':
            original=next(obj for obj in objects if obj['name']==name)
            original['parent']=zh_group
        else:
            label(name+'_'+role,34+i*26,25,26,32,character,25,ACCENTS[role],zh_group,font='fonts/NotoSansSC-Bold.otf')
    label('role_title_'+role+'_en',34,26,110,32,data[1],23,ACCENTS[role],en_group,font=LATIN_BOLD)
    for lang,parent in [('zh',zh_group),('en',en_group)]:
        suffix='' if lang=='zh' else '_en'
        original=next(obj for obj in objects if obj['name']=='capture_status'+suffix)
        if role=='pulse':original['parent']=parent
        else:
            clone=dict(original);clone.update(name='capture_status_'+role+suffix,bind='capture_status_'+role+suffix,parent=parent,fg_color='#a46848');objects.append(clone)

contract['home_geometry']={
    'art':[90,89,300,204], 'volume':[16,89,64,207], 'richness':[400,89,64,207],
    'progress':[103,301,274,2], 'feedback':[90,315,300,15], 'css_steps':[90,316,300,13],
    'state_line':[24,340,432,23], 'role':[22,378,84,84], 'settings':[374,378,84,84],
    'header_en':{'pulse':{'separator_x':103,'phrase_x':117},'voice':{'separator_x':102,'phrase_x':115},'dust':{'separator_x':93,'phrase_x':107},'harmony':{'separator_x':142,'phrase_x':155}},
}
contract['strip']={'bounds':[118,378,244,84], 'radius':23,
    'hit_targets':{'record':[118,378,122,84],'record_empty':[118,378,244,84],'transport':[240,378,122,84],'transport_only':[118,378,244,84],'strip_action':[118,378,244,84]},
    'modes':{'record_only':{'record':False,'record_empty':True,'transport':False,'transport_only':False,'strip_action':False},'play_only':{'record':False,'record_empty':False,'transport':False,'transport_only':True,'strip_action':False},'idle':{'record':True,'record_empty':False,'transport':True,'transport_only':False,'strip_action':False},'cancel':{'record':False,'record_empty':False,'transport':False,'transport_only':False,'strip_action':True},'pause':{'record':False,'record_empty':False,'transport':False,'transport_only':False,'strip_action':True}},
    'border':'strip_border', 'border_colors':{'idle':'#d2d9c9','cancel':'#d5a98d','pause':'#455b3b'},
    'tints':{'record':{'name':'strip_record_tint','x':[-244,0],'default_x':-122},'play':{'name':'strip_play_tint','x':[0,244],'default_x':122}},
    'tint_width':244, 'tint_boundary':'record.x + 244 == play.x', 'tint_alpha':'RGB888 preblend over #e7ecdf before set_color_rgb888; no unsupported packed-color alpha',
    'idle_disabled_glyph_alpha':1.0,
    'css_easing':[0,0,.58,1], 'duration_ms':{'tint_geometry':240,'tint_color':240,'tint_opacity':200,'divider':150,'idle_opacity':130,'idle_translation':240,'single_opacity':200,'single_scale':240},
}
# Native text is retained as the stable fallback. The controller can replace
# only these glyph groups with the separately tested transparent image path.
contract['strip']['native_labels']={}
for name,zh,en,asset,color,origin,width,icon_size in [
    ('record','录制','Record','record','#aa6140',118,122,19),
    ('play','播放','Play','play','#536648',240,122,23),
    ('cancel','取消','Cancel','cross','#a36144',118,244,23),
    ('pause','暂停','Pause','pause','#f8f8ee',118,244,23),
]:
    contract['strip']['native_labels'][name]={}
    for lang,word in [('zh',zh),('en',en)]:
        suffix='' if lang=='zh' else '_en'
        frame=add('container',f'strip_{name}_frame'+suffix,origin,378,width,84,home_text[lang])
        shift={'default':0,'min':-8,'max':0} if name=='record' else {'default':0,'min':0,'max':8} if name=='play' else 0
        group=layer(f'strip_{name}_native'+suffix,frame,x=shift,w=width,h=84,hidden=True)
        contract['strip']['native_labels'][name][lang]=f'strip_{name}_native'+suffix
        font=REGULAR if lang=='zh' else LATIN
        size=17 if lang=='zh' else 16
        gap=9 if lang=='zh' else 8
        text_width=ImageFont.truetype(str(UI/font),size).getlength(word)
        left=(width-icon_size-gap-text_width)/2
        icon(('record_icon' if name=='record' else 'play_icon' if name=='play' else 'cancel_icon' if name=='cancel' else 'pause_icon')+suffix,asset,round(left),round((84-icon_size)/2),icon_size,icon_size,group,tint=color)
        dyn(('record_text' if name=='record' else 'play_text' if name=='play' else 'cancel_text' if name=='cancel' else 'pause_text')+suffix,round(left+icon_size+gap),28 if lang=='zh' else 30,math.ceil(text_width)+2,24,word,size,color,group,font=font)
icon('strip_corners','strip-corners',0,0,244,84,strip)
icon('strip_border','strip-border',0,0,244,84,strip,tint='#d2d9c9')

# Only the eight fixed strip icon+word glyphs need alpha/scale animation.
# They are transparent masks, never a screenshot of a control or its surface.
# The public RGBA image COPY path applies continuous alpha on these A8 masks;
# background fills, border, buttons, header and dynamic status remain native.
(UI/'strip').mkdir(exist_ok=True)
GLYPH_W,GLYPH_H=96,32
# A newly shown single action must stay transparent until its first async
# COPY publish completes; never flash a full-opacity static fallback frame.
Image.new('RGBA',(GLYPH_W,GLYPH_H),(0,0,0,0)).save(UI/'strip'/'single-placeholder.png')
glyph_header=['/* Generated by tools/generate_scene.py from pinned fonts and original SVG icons. */',
    '#pragma once','#include <stdint.h>',
    '#define ECHOY_STRIP_GLYPH_WIDTH 96','#define ECHOY_STRIP_GLYPH_HEIGHT 32',
    'typedef enum { ECHOY_STRIP_GLYPH_RECORD, ECHOY_STRIP_GLYPH_PLAY, ECHOY_STRIP_GLYPH_CANCEL, ECHOY_STRIP_GLYPH_PAUSE } echoy_strip_glyph_kind_t;',
    'typedef struct { uint16_t width, height, stride; uint32_t color_rgb888; const uint8_t *alpha; } echoy_strip_glyph_mask_t;']
glyph_rows=[]
glyph_specs=[('record','录制','Record','record','#aa6140',19),('play','播放','Play','play','#536648',23),('cancel','取消','Cancel','cross','#a36144',23),('pause','暂停','Pause','pause','#f8f8ee',23)]
for lang,language_index in [('zh',0),('en',1)]:
    rows=[]
    for name,zh,en,asset,color,icon_size in glyph_specs:
        word=zh if lang=='zh' else en
        size=17 if lang=='zh' else 16
        font=ImageFont.truetype(str(UI/(REGULAR if lang=='zh' else LATIN)),size)
        gap=9 if lang=='zh' else 8
        left=(GLYPH_W-icon_size-gap-font.getlength(word))/2
        svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="{icon_size}" height="{icon_size}" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">{icons[asset].replace("currentColor","#ffffff")}</svg>'
        icon_rgba=Image.open(BytesIO(cairosvg.svg2png(bytestring=svg.encode()))).convert('RGBA')
        rgba=Image.new('RGBA',(GLYPH_W,GLYPH_H),(255,255,255,0))
        rgba.alpha_composite(icon_rgba,(round(left),round((GLYPH_H-icon_size)/2)))
        ascent,descent=font.getmetrics()
        # The HTML's Arial-first line box sets the baseline even when the
        # characters fall back to Noto; its CJK ink is 2px above Noto's own box.
        ImageDraw.Draw(rgba).text((left+icon_size+gap,(GLYPH_H-ascent-descent)/2+ascent-(2 if lang=='zh' else 0)),word,font=font,fill=(255,255,255,255),anchor='ls')
        alpha=rgba.getchannel('A')
        symbol=f'echoy_strip_alpha_{lang}_{name}'
        glyph_header.append(f'static const uint8_t {symbol}[ECHOY_STRIP_GLYPH_WIDTH * ECHOY_STRIP_GLYPH_HEIGHT] = {{')
        data=alpha.tobytes()
        glyph_header.extend('    '+','.join(str(v) for v in data[at:at+24])+',' for at in range(0,len(data),24))
        glyph_header.append('};')
        rows.append(f'    {{96,32,96,0x{color[1:]}u,{symbol}}},')
        foreground=Image.new('RGBA',(GLYPH_W,GLYPH_H),color)
        foreground.putalpha(alpha)
        foreground.save(UI/'strip'/f'{name}-{lang}.png')
    glyph_rows.append(rows)
glyph_header.append('/* First index: 0 = Chinese, 1 = English; second index: echoy_strip_glyph_kind_t. */')
glyph_header.append('static const echoy_strip_glyph_mask_t echoy_strip_glyph_masks[2][4] = {')
for rows in glyph_rows:glyph_header.extend(['  {',*rows,'  },'])
glyph_header.append('};')
(UI/'strip'/'labels.h').write_text('\n'.join(glyph_header)+'\n')
(UI/'strip'/'play-initial.png').unlink(missing_ok=True)
contract['strip']['glyphs']={}
for name,x,low,high in [('record',131,123,192),('play',253,192,261),('cancel',192,192,192),('pause',192,192,192)]:
    source='single-placeholder.png' if name in ('cancel','pause') else name+'-zh.png'
    options={} if name in ('record','play') else dict(scalable=True,scale=.97,min_scale=.97,max_scale=1.0)
    add('image','strip_'+name+'_glyph',{'default':x,'min':low,'max':high} if low!=high else x,404,96,32,home,image='strip/'+source,codec='raw' if name in ('cancel','pause') else 'lossless',dynamic_image=True,bind='strip_'+name+'_glyph',bind_target='resource',hidden=True,**options)
    contract['strip']['glyphs'][name]={'name':'strip_'+name+'_glyph','bounds':[x,404,96,32],'x_range':[low,high],'scale':[.97,1] if name in ('cancel','pause') else [1,1],'alpha':'runtime RGBA QOI COPY from echoy_strip_glyph_masks[english][kind]'}
contract['strip']['glyph_header']='strip/labels.h'

# Role / phrase gallery.
role_page=route('role')
header('role',role_page,'角色与表达','Role & phrase')
role_text=locales('role',role_page)
text_pair('role_header',108,33,274,31,'角色与表达','Role & phrase',23,'#34392f',role_text,en_size=21,weight=True,text_align='center',overflow='ellipsis')
for side,x,callback,asset in [('prev',28,'prev_role_click','back'),('next',368,'next_role_click','next')]:
    parent=button(side+'_role',x,117,84,124,role_page,callback,bg_color='#ecefe4',border_color='#d6dccd',border_width=1,radius=18)
    add('image','role_'+side+'_art',6,10,73,48,parent,image='role-neighbor.png',codec='lossless',dynamic_image=True,bind='role_'+side+'_art',bind_target='resource')
    icon('role_'+side+'_arrow',asset,32,96,19,19,parent,tint='#738263')
card=add('container','role_card',124,105,232,141,role_page,bg_color='#e5e7de',border_color='#d4dbca',border_width=1,radius=23)
add('image','role_main_art',13,3,206,101,card,image='role-main.png',codec='lossless',dynamic_image=True,bind='role_main_art',bind_target='resource')
for i,heights in enumerate(([8,17,6,12,5],[14,6,11,5,16],[8,12,18,14,10]),1):
    b=button('phrase_'+str(i),24+(i-1)*148,284,136,84,role_page,'phrase_'+str(i)+'_click',bg_color='#eceee3',border_color='#d0d8c4',border_width=1,radius=17)
    selected=layer('phrase_'+str(i)+'_selected',b,w=136,h=84,hidden=i!=1)
    surface=add('container','phrase_'+str(i)+'_selected_surface',0,0,136,84,selected,bg_color='#d95535',radius=17,bind='phrase_'+str(i)+'_selected_color',bind_target='color')
    add('container','phrase_'+str(i)+'_selected_inner',1,1,134,80,surface,bg_color='#e5e7de',radius=16)
    for j,height in enumerate(heights):
        add('container',f'phrase_{i}_bar_{j}',53+j*7,36-height,3,height,b,bg_color='#d95535',radius=2,bind=f'phrase_{i}_bar_{j}_color',bind_target='color')
for role,data in ROLES.items():
    index=list(ROLES).index(role);before=list(ROLES)[(index+3)%4];after=list(ROLES)[(index+1)%4]
    contract['role_copy'][role]={}
    for lang,language_index in [('zh',0),('en',1)]:
        group=layer('role_copy_'+role+'_'+lang,role_text[lang],hidden=role!='pulse')
        contract['role_copy'][role][lang]='role_copy_'+role+'_'+lang
        font=REGULAR if lang=='zh' else LATIN
        label('role_name_'+role+'_'+lang,126,207,228,30,data[language_index],22,ACCENTS[role],group,font=MEDIUM if lang=='zh' else LATIN_BOLD,text_align='center')
        label('role_note_'+role+'_'+lang,26,253,428,20,data[4+language_index],12,'#7c886e',group,font=font,text_align='center')
        for side,x,neighbor in [('prev',28,before),('next',368,after)]:
            label(f'role_{side}_name_{role}_{lang}',x+1,182,82,22,ROLES[neighbor][language_index],14,'#738263',group,font=font,text_align='center')
        for i,content in enumerate(data[2+language_index],1):
            label(f'phrase_{i}_label_{role}_{lang}',25+(i-1)*148,329,134,22,content,17,'#637555',group,font=MEDIUM if lang=='zh' else LATIN,text_align='center',font_charset=content,bind=f'phrase_{i}_label_{role}_{lang}_color',bind_target='color')
gallery=button('gallery_transport',120,380,240,84,role_page,'audition_click',bg_color='#e4eadb',border_color='#becbb1',border_width=1,radius=19)
icon('gallery_play','play',61,30,24,24,gallery,tint='#536b40',hidden=True)
icon('gallery_pause','pause',61,30,24,24,gallery,tint='#536b40',hidden=True)
text_pair('gallery_text',216,408,134,28,'试听一次','Hear once',19,'#536b40',role_text)
# The paused label has a separate dynamic corpus; both fit in the same space.
for obj in objects:
    if obj['name']=='gallery_text':obj['font_charset']='试听一次暂停本机'
    if obj['name']=='gallery_text_en':obj['font_charset']='Hear once Pause this tile'

# Approved settings revision; the frozen prototype and home controls stay intact.
def settings_page(name,zh,en):
    page=route(name); header(name,page,zh,en)
    heads=locales(name+'_header',page)
    text_pair(name+'_header_text',108,33,274,31,zh,en,23,'#34392f',heads,en_size=21,weight=True,text_align='center')
    return page

settings_root=settings_page('settings','设置','Settings')
settings_text=locales('settings',settings_root)
for i,(name,asset,zh,en) in enumerate([
    ('display','display','显示','Display'),
    ('language',None,'语言','Language'),
    ('guide','help','怎么玩','How to play'),
    ('info','info','关于设备','About this tile')]):
    y=108+i*88
    style=dict(bg_color='#ecefe5',border_color='#d9e0d0',border_width=1,radius=17)
    row=add('container','settings_language',24,y,432,78,settings_root,**style) if name=='language' else button('settings_'+name,24,y,432,78,settings_root,name+'_click',**style)
    if asset:
        icon('settings_'+name+'_icon',asset,20,28,22,22,row,tint='#576b47')
        icon('settings_'+name+'_next','next',388,28,22,22,row,tint='#576b47')
    text_pair('settings_'+name+'_text',79 if asset else 44,y+24,270 if asset else 175,30,zh,en,18,'#576b47',settings_text,en_size=17)
    if name=='language':
        for lang,x,word,font in [('zh',244,'中文',REGULAR),('en',336,'EN',LATIN)]:
            b=button('language_'+lang,x,y+7,84,64,settings_root,'lang_'+lang+'_click',bg_color='#e6ecdc',border_color='#c9d4bc',border_width=1,radius=13)
            selected=layer('language_'+lang+'_selected',b,w=84,h=64,hidden=lang=='en')
            add('container','language_'+lang+'_selected_surface',0,0,84,64,selected,bg_color='#d6e1ca',border_color='#8a9c78',border_width=1,radius=13)
            label('language_'+lang+'_label',0,18,84,28,word,17,'#34392f',b,font=font,text_align='center')

display_page=settings_page('display','显示','Display')
display_copy=locales('display',display_page)
text_pair('display_brightness_text',42,120,230,30,'屏幕亮度','Brightness',18,'#576b47',display_copy)
text_pair('display_percent',336,120,102,30,'100%','100%',18,'#576b47',display_copy,text_align='right')
for obj in objects:
    if obj['name'] in ('display_percent','display_percent_en'):obj['font_charset']='0123456789%'
add('container','display_rail',62,190,356,6,display_page,bg_color='#c3cdb6',radius=3)
add('container','display_knob',{'default':404,'min':48,'max':404},179,28,28,display_page,bg_color='#fafbf5',border_color='#93a382',border_width=1,radius=14)
text_pair('display_soft',42,224,100,24,'柔和','Soft',14,'#7c886e',display_copy)
text_pair('display_bright',338,224,100,24,'明亮','Bright',14,'#7c886e',display_copy,text_align='right')
text_pair('display_note_1',42,278,396,26,'屏幕保持设定的亮度。','Brightness stays at your chosen level.',15,'#7c886e',display_copy)
for obj in objects:
    if obj['name']=='display_note_1':obj['font_charset']+='亮度调整未成功，请再试一次。'
    if obj['name']=='display_note_1_en':obj['font_charset']+='Could not set brightness. Please try again.'
# A native overlay represents brightness on the simulator. Hardware uses its panel.
# It is declared after the other pages below so it never covers future controls.

guide_page=settings_page('guide','怎么玩','How to play')
guide_copy=locales('guide',guide_page)
steps=[
    ('先听听声音','Try a sound','轻点屏幕中央，试试这个角色的声音。','Tap the center to try this role’s sound.','到“角色”里，还能换一种声音。','Open Role to choose a different sound.'),
    ('录下你的节奏','Record your rhythm','点“录制”，等倒数结束后再敲。','Tap Record. Wait for the countdown, then tap.','录制共四拍，结束后会回放一次。','You have 4 beats. Your rhythm plays back once.'),
    ('让节奏继续','Keep it playing','点“播放”循环，点“暂停”停下。','Tap Play to repeat your rhythm; Pause to stop.','想听哪台，就点那台的“播放”。','Tap Play on each tile you want to hear.'),
    ('再加一台试试','Try adding another tile','再找一台装好 Echoy 的 Mosaico','Put another Mosaico running Echoy on the right.','同朝向贴到右侧，最多支持三台。','Face both the same way. Connect up to 3 tiles.')]
for i,step in enumerate(steps):
    y=108+i*86
    label('guide_step_'+str(i),38,y+7,30,30,str(i+1),18,'#879571',guide_page,font=LATIN)
    text_pair('guide_title_'+str(i),86,y,352,28,*step[:2],18,'#4f6340',guide_copy,weight=True)
    for line in range(2):
        text_pair('guide_body_'+str(i)+'_'+str(line),86,y+30+line*22,352,22,*step[2+line*2:4+line*2],15,'#637555',guide_copy,en_size=14)

info_page=settings_page('info','关于设备','About this tile')
info_copy=locales('info',info_page)
label('info_brand',42,120,396,40,'Echoy',30,'#4f6340',info_page,font=LATIN_BOLD)
text_pair('info_tagline',42,166,396,28,'让你的节奏，有个回应。','A reply to your rhythm.',17,'#7c886e',info_copy)
for i,(name,zh,en,value,en_value) in enumerate([
    ('platform','设备','Device','Mosaico','Mosaico'),
    ('version','软件版本','Software',VERSION,VERSION),
    ('battery','电量','Battery','--%','--%')]):
    y=232+i*66
    add('container','info_divider_'+name,42,y-12,396,1,info_page,bg_color='#dce0d2')
    text_pair('info_label_'+name,42,y,176,28,zh,en,16,'#7c886e',info_copy)
    text_pair('info_value_'+name,208,y,230,28,value,en_value,17,'#34392f',info_copy,text_align='right')
for obj in objects:
    if obj['name'].startswith(('info_value_','info_label_battery')):
        obj['font_charset']+=' ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789%._+-'
        if not obj['name'].endswith('_en'):obj['font_charset']+='电脑模拟本地构建模拟电量未知'
contract['detail_defaults']={}

status_page=route('status')
icon('status_art','wave',210,160,60,60,status_page,tint='#a9693f',scalable=True,min_scale=.85,max_scale=1.0)
# Scene images do not support a dynamic opacity field in GSPC 0.6.1.
# A paper-colored native fill above the icon reproduces its breathing fade.
add('container','status_art_fade',210,160,60,60,status_page,bg_color=PAPER,opacity={'default':0,'min':0,'max':204})
status_text=locales('status_page',status_page)
text_pair('problem_title',36,243,408,33,'暂时无法发声','Audio unavailable',25,'#34392f',status_text,weight=True,text_align='center')
text_pair('problem_body',40,292,400,27,'节奏保留，恢复不会自动发声。','Rhythm is kept. Recovery never autoplays.',16,'#7d8b6f',status_text,text_align='center')
recover=button('recover',36,372,408,84,status_page,'recover_click',bg_color='#e1e9d7',border_color='#c6d2b6',border_width=1,radius=19)
text_pair('recover_text',36,398,408,32,'恢复到静止','Restore, stay silent',18,'#5a7046',status_text,text_align='center')
for obj in objects:
    if obj['name']=='problem_title':obj['font_charset']='暂时无法发声电量已耗尽准备声音'
    if obj['name']=='problem_title_en':obj['font_charset']='Audio unavailable Battery empty Preparing sound'
add('container','display_shade',0,0,480,480,paper,bg_color='#000000',radius=60,opacity={'default':0,'min':0,'max':204})

# A temporary native layer above the pad, with no button or pointer interception.
# Reuse established font sizes to remain within GSP's 32-font-pack limit.
hint_popup=layer('record_hint_popup',home,x=90,y=127,w=300,h=128)
add('container','record_hint_surface',0,0,300,128,hint_popup,bg_color='#fafbf3',border_color='#a9b99a',border_width=1,radius=20)
hint_copy=locales('record_hint',hint_popup,w=300,h=128)
text_pair('record_hint_first',16,26,268,34,'点录制，再敲一遍，','Tap Record, then tap again',22,'#4f6340',hint_copy,en_size=18,weight=True,dynamic=False,text_align='center')
text_pair('record_hint_second',16,68,268,34,'就能留下节奏。','to keep your rhythm.',22,'#4f6340',hint_copy,en_size=18,weight=True,dynamic=False,text_align='center')

# Later paint order must leave labels above surfaces. Locale groups and role
# copy groups were declared before their shapes so move complete group trees
# behind neither the buttons nor the Canvas targets. A stable topological
# traversal also puts parents before children after home-label relocation.
for page_name in ('home','role_page','settings_page','display_page','guide_page','info_page','status_page'):
    page_index=next(i for i,obj in enumerate(objects) if obj['name']==page_name)
    children=[i for i,obj in enumerate(objects) if obj['parent']==page_index]
    text_children=[i for i in children if any(i in pair.values() for pair in (home_text,role_text,settings_text,display_copy,guide_copy,info_copy,status_text)) or objects[i]['name'].endswith(('_header_zh','_header_en'))]
    for i in text_children:objects[i]['_last']=True

for obj in objects:
    if obj['type']=='button':
        obj['disabled_color']='#f1efe8'; obj['disabled_opacity']=0 if obj['name'] in ('record','record_empty','transport','transport_only','strip_action') else 145
    if obj['name'] in ('volume_knob','volume_fill','richness_knob','richness_fill'):
        y=obj['y']; obj['y']={'default':y,'min':0,'max':180}
    if obj['name'].startswith('step_'):
        obj['bind']=obj['name']+'_color'; obj['bind_target']='color'
# Fill and progress colors follow the selected/committed role.
for obj in objects:
    if obj['name'] == 'richness_fill':
        obj.update(bind=obj['name']+'_color',bind_target='color')
    if obj.get('callback'):contract['callbacks'].append(obj['callback'])
# GSP 1.5.1 exposes one primary bind family per progress widget.
# Share values across four fixed-color variants instead of replacing VALUE with COLOR.
progress_object=next(obj for obj in objects if obj['name']=='progress')
progress_object.update(bind='progress_value',bind_target='value')
for role in ('voice','dust','harmony'):
    clone=dict(progress_object)
    clone.update(name='progress_'+role,bind='progress_'+role+'_value',fg_color=ACCENTS[role])
    objects.append(clone)
# The original richness thumb and its '=' glyph follow the committed role.
knob_index=next(i for i,obj in enumerate(objects) if obj['name']=='richness_knob')
objects[knob_index].update(bg_color=ACCENTS['pulse'],bind='richness_knob_color',bind_target='color')
objects[knob_index].pop('border_color',None)
objects[knob_index].pop('border_width',None)
add('container','richness_knob_inner',1,1,28,25,knob_index,bg_color='#fafbf5',radius=9)
equal=next(obj for obj in objects if obj['name']=='equal')
equal.update(fg_color=ACCENTS['pulse'],font_charset='=',bind='richness_equal_color',bind_target='color',_last=True)
# Labels retain their TEXT bind; parent layers own runtime visibility.
for suffix in ('','_en'):
    hint=next(obj for obj in objects if obj['name']=='touch_hint'+suffix)
    hint['parent']=layer('touch_hint_group'+suffix,hint['parent'],hidden=False)
old=objects;objects=[]
def emit(parent):
    children=[(i,obj) for i,obj in enumerate(old) if obj['parent']==parent]
    children.sort(key=lambda pair:bool(pair[1].get('_last')))
    for index,obj in children:
        new_index=len(objects);obj=dict(obj);obj.pop('_last',None);obj['parent']=-1 if parent==-1 else relocated[parent];objects.append(obj);relocated[index]=new_index;emit(index)
relocated={};emit(-1)
scene=dict(screen='echoy',w=480,h=480,screen_bg='#363b30',font='fonts/NotoSansSC-Regular.otf',default_font_size=16,swipe=False,objects=objects)
(UI/'main.json').write_text(json.dumps(scene,ensure_ascii=False,indent=2)+'\n')
contract['callbacks']=sorted(set(contract['callbacks']))
(UI/'page-contract.json').write_text(json.dumps(contract,ensure_ascii=False,indent=2)+'\n')
print('Generated native scene:',len(objects),'objects')

from generate_view_keys import generate as generate_view_keys
generate_view_keys(UI / "main.json", ROOT / "projects/echoy/main/echoy_view_keys.h")
