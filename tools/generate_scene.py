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
from PIL import Image, ImageFont
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
for name,path in icons.items():
    color='#fafbf3' if name in ('play','pause') else '#b26950' if name in ('record','cross') else '#5f7450'
    svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><g id="icon">{path.replace("currentColor",color)}</g></svg>'
    (UI/'icons'/(name+'.svg')).write_text(svg)
Image.new('RGB',(300,204),(241,239,232)).save(UI/'canvas.png')
Image.new('RGB',(300,15),(241,239,232)).save(UI/'feedback.png')
Image.new('RGB',(206,101),(229,231,222)).save(UI/'role-main.png')
Image.new('RGB',(73,48),(236,239,228)).save(UI/'role-neighbor.png')
(UI/'icons/pad-border.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="300" height="204" viewBox="0 0 300 204"><g id="icon"><rect x=".5" y=".5" width="299" height="203" rx="29.5" fill="none" stroke="#ccd4c1" stroke-width="1" stroke-dasharray="3 3"/></g></svg>')
# GSPC 0.6.1 does not preserve SVG stroke-dasharray. Rasterize this small
# static decoration; controls, text, music art and feedback remain native.
cairosvg.svg2png(url=str(UI/'icons/pad-border.svg'),write_to=str(UI/'icons/pad-border.png'))
objects=[]
def add(kind,name,x,y,w,h,parent=-1,**kw):
    obj=dict(type=kind,parent=parent,name=name,x=x,y=y,w=w,h=h,**kw); objects.append(obj); return len(objects)-1
# Runtime glyphs are scoped to the actual UI text, never HTML/CSS/JavaScript.
# GSP embeds one font pack per (font,size); an all-source corpus at every size
# makes even the old home-only scene almost fill the 1 MiB UI partition.
home_status_zh='先试着敲几下准备录制预备 · 1234录入 / 准备新的声音下一小节生效等共同拍点试听先听这一次回应左侧正在演奏已接住左侧的节奏这段留在这里了'
home_status_en='Try tapping the centre Preparing to record Ready · 1234 Recording / Preparing a new sound Changes at the next bar Audition at the shared beat Hear this one reply Following left Playing Left rhythm received Your rhythm is here'
home_hint_zh='新声音没有准备好，原来的继续 点「录一段」才记录节奏开头可以留空 · 提示音不会录入原来的继续，可随时暂停本机已经收到，不会中途重编结束就停 · 点 ▶ 才持续演奏等共同拍点开始Ⅱ 只暂停本机，不改变节奏▶ 开始演奏 · 不自动改写已取消，原节奏保留没有录入，原节奏保留'
home_hint_en='Preparation failed; current sound continues Press Record to capture your rhythm You may start with silence · Cues are not recorded Current sound continues; Pause stays available Received · Current phrase stays intact Stops after one reply · Play to keep going Waiting for the shared beat Pause affects this tile, not the rhythm Play when ready · No automatic changes Cancelled; original kept No notes captured; original kept'
role_names_zh='脉冲声线微尘和声'
phrase_names_zh='重音切分留白短句轻答长线颗粒点缀余韵短和弦分解接句'
phrase_names_en='Accent Syncopation Space Short phrase Light reply Long line Grains Touches Trails Short chords Arpeggio Answer'
home_charsets={'role_title_first':role_names_zh,'role_title_second':role_names_zh,'phrase_title':phrase_names_zh,'battery_text':'0123456789%','record_text':'录一段取消','status':home_status_zh,'capture_status':home_status_zh,'touch_hint':'试奏区','hint':home_hint_zh}
charset=home_status_zh
def label(name,x,y,w,h,content,size,color,parent,**kw):
    return add('label',name,x,y,w,h,parent,text=content,font_size=size,fg_color=color,**kw)
def dyn(name,x,y,w,h,content,size,color,parent,**kw):
    corpus=kw.pop('font_charset',home_charsets.get(name,content))
    return label(name,x,y,w,h,content,size,color,parent,bind=name,bind_target='text',font_charset=corpus,**kw)
paper=add('container','paper',0,0,480,480,bg_color='#f1efe8',radius=60,clip_children=True)
home=add('layer','home',0,0,480,480,paper,bind='home_visible',bind_target='visible')
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
    add('container',name+'_battery_fill',410,45,9,6,parent,bg_color='#78836c')
    dyn(name+'_battery_text',435,35,25,22,'72%',11,'#78836c',parent,font='fonts/DejaVuSansMono.ttf',font_charset='0123456789%')
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
    'phrase_title':('Accent',13,phrase_names_en),
    'record_text':('Record',16,'Record Cancel'),
    'cancel_text':('Cancel',16,'Cancel'),
    'status':('Try tapping the centre',14,home_status_en),
    'capture_status':('Try tapping the centre',14,home_status_en),
    'volume_bottom':('Volume',14,'Volume'),
    'richness_top':('Rich',14,'Rich'),
    'richness_bottom':('Simple',14,'Simple'),
    'touch_hint':('Try here',13,'Try here'),
    'hint':('Press Record to capture your rhythm',12,home_hint_en),
    'role_label':('Role',14,'Role'),
    'settings_label':('Settings',14,'Settings'),
}
for obj in list(objects):
    if obj['name'] not in home_en:continue
    content,size,corpus=home_en[obj['name']]
    clone=dict(obj);clone.update(name=obj['name']+'_en',parent=home_text['en'],text=content,font_size=size,font=LATIN,font_charset=corpus,bind=obj['name']+'_en',bind_target='text')
    if obj['name'] in ('role_label','settings_label'):
        clone.update(w=68,font=LATIN_BOLD)
        clone['x']=99 if obj['name']=='role_label' else 372
    objects.append(clone)
    if obj['name'] in ('cancel_text','volume_bottom','richness_top','richness_bottom','role_label','settings_label'):
        obj.update(bind=obj['name'],bind_target='text',font_charset=obj['text'])
for name,x,english_x in [('role_icon',66,69),('settings_icon',350,342)]:
    original=next(obj for obj in objects if obj['name']==name)
    original.update(parent=home_text['zh'],x=x,y=411)
    clone=dict(original);clone.update(name=name+'_en',parent=home_text['en'],x=english_x);objects.append(clone)
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
            label(name+'_'+role,35+i*26,23,26,31,character,25,ACCENTS[role],zh_group,font='fonts/NotoSansSC-Bold.otf')
    label('role_title_'+role+'_en',35,23,190,31,data[1],25,ACCENTS[role],en_group,font=LATIN_BOLD)
    for lang,parent in [('zh',zh_group),('en',en_group)]:
        suffix='' if lang=='zh' else '_en'
        original=next(obj for obj in objects if obj['name']=='capture_status'+suffix)
        if role=='pulse':original['parent']=parent
        else:
            clone=dict(original);clone.update(name='capture_status_'+role+suffix,bind='capture_status_'+role+suffix,parent=parent,fg_color=ACCENTS[role]);objects.append(clone)

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

# Settings: fixed native controls inside a clipped, movable content subtree.
settings_page=route('settings')
header('settings',settings_page,'设置','Settings')
settings_head=locales('settings_header',settings_page)
text_pair('settings_header_text',108,33,274,31,'设置','Settings',23,'#34392f',settings_head,en_size=21,weight=True,text_align='center')
settings_content=scroll_area('settings',settings_page,432,350,702,24,108)
settings_text=locales('settings',settings_content,w=426,h=702)
settings_rows=[
    ('audition','play','先听一次','Hear once','audition_click'),
    ('record','record','录一段新的节奏','Record a new rhythm','record_click'),
    ('guide','help','怎么玩','How to play','guide_click'),
    ('info','wave','设备与实验说明','Device & experiment','info_click'),
    ('export','download','导出本次节奏','Export this session','export_click'),
]
for i,(name,asset,zh,en,callback) in enumerate(settings_rows):
    row=button('settings_'+name,0,i*96,426,84,settings_content,callback,bg_color='#ecefe5',border_color='#d9e0d0',border_width=1,radius=17)
    icon('settings_'+name+'_icon',asset,20,31,22,22,row,tint='#576b47')
    text_pair('settings_'+name+'_text',55,i*96+27,335 if name not in ('guide','info') else 300,30,zh,en,18,'#576b47',settings_text,en_size=16)
    if name in ('guide','info'):icon('settings_'+name+'_next','next',384,31,22,22,row,tint='#576b47')
for obj in objects:
    if obj['name']=='settings_record_text':obj['font_charset']+='先拆开左侧，再录自己的节奏'
    if obj['name']=='settings_record_text_en':obj['font_charset']+='Unlink left to record your rhythm'
language_row=add('container','settings_language',0,480,426,114,settings_content,bg_color='#ecefe5',border_color='#d9e0d0',border_width=1,radius=17)
text_pair('settings_language_text',20,522,190,30,'语言','Language',18,'#576b47',settings_text,en_size=16)
for lang,x,word,font in [('zh',230,'中文',REGULAR),('en',322,'EN',LATIN)]:
    b=button('language_'+lang,x,15,84,84,language_row,'lang_'+lang+'_click',bg_color='#e6ecdc',border_color='#c9d4bc',border_width=1,radius=13)
    label('language_'+lang+'_label',0,28,84,28,word,17,'#34392f',b,font=font,text_align='center')
remove=layer('settings_remove_group',settings_content,x=0,y=606,w=426,h=84,hidden=False,enabled=True)
remove_button=button('settings_remove',0,0,426,84,remove,'remove_click',bg_color='#ecefe5',border_color='#d9e0d0',border_width=1,radius=17)
icon('settings_remove_icon','cross',20,31,22,22,remove_button,tint='#576b47')
remove_text=locales('settings_remove',remove,w=426,h=84)
text_pair('settings_remove_text',55,27,350,30,'收起这台（模拟）','Put away this tile',18,'#576b47',remove_text,en_size=16)
contract['scroll']['settings']['height']={'zh':702,'en':702,'hardware':606}

# Match CSS line wrapping with the same pinned fonts. Explicit newline layout
# avoids relying on dynamic text to reflow native labels at runtime.
latin_cmap=TTFont(UI/LATIN).getBestCmap()
def text_runs(content,font):
    runs=[]
    for character in content:
        selected=(LATIN_BOLD if font==MEDIUM else LATIN) if font in (REGULAR,MEDIUM) and ord(character) in latin_cmap else font
        if runs and runs[-1][0]==selected:runs[-1]=(selected,runs[-1][1]+character)
        else:runs.append((selected,character))
    return runs

def text_width(content,font,size):
    return sum(ImageFont.truetype(str(UI/run_font),size).getlength(run) for run_font,run in text_runs(content,font))

def wrapped(content,font,size,width):
    lines=[]
    for paragraph in content.split('\n'):
        if not paragraph:lines.append('');continue
        tokens=re.findall(r'[A-Za-z0-9]+(?:[’\'-][A-Za-z0-9]+)*\s*|.',paragraph)
        line=''
        for token in tokens:
            if line and text_width((line+token).rstrip(),font,size)>width:
                lines.append(line.rstrip());line=token.lstrip()
            else:line+=token
        lines.append(line.rstrip())
    return lines

guide_source=text.split("if(guide)html=`",1)[1].split('`;\n  else html=',1)[0]
guide_pairs=re.findall(r"this.t\('([^']*)','([^']*)'\)",guide_source)
info_source=text.split('else html=`<h3>',1)[1].split('`;\n  return',1)[0]
info_pairs=re.findall(r"this.t\('([^']*)','([^']*)'\)",info_source)
assert len(guide_pairs)==5 and len(info_pairs)==6
detail_defaults={}
for name,title,pairs in [('guide',('怎么玩','How to play'),guide_pairs),('info',('设备与实验说明','Device & experiment'),info_pairs)]:
    page=route(name);header(name,page,*title)
    heads=locales(name+'_header',page)
    text_pair(name+'_header_text',108,33,274,31,*title,23,'#34392f',heads,en_size=21,weight=True,text_align='center',overflow='ellipsis')
    content=scroll_area(name,page,410,345,1400,35,108)
    copy=locales(name,content,w=410,h=1400)
    detail_defaults[name]={}
    for lang,language_index in [('zh',0),('en',1)]:
        y=10
        if name=='guide':blocks=[('heading',pairs[0][language_index]),('body',pairs[1][language_index]),('heading',pairs[2][language_index]),('body',pairs[3][language_index]),('body',pairs[4][language_index])]
        else:blocks=[('heading',pairs[0][language_index]),('body',pairs[1][language_index]),('device',('本机 A · 72% · IMU 0°\n左 / 右扩展: none / none\n来自左侧: —' if lang=='zh' else 'Tile A · 72% · IMU 0°\nLeft / right slot: none / none\nFollowing: —')),('small',pairs[5][language_index])]
        detail_defaults[name][lang]=[]
        for index,(kind,content_text) in enumerate(blocks):
            size=23 if kind=='heading' else 13 if kind=='small' else 17 if lang=='zh' else 16
            font=(MEDIUM if lang=='zh' else LATIN_BOLD) if kind=='heading' else REGULAR if lang=='zh' else LATIN
            # <small> is inline, so the parent's 30.6/28.8 px line strut
            # determines its baseline spacing despite its own 21.45 px rule.
            line_height=32.2 if kind=='heading' else 30.6 if lang=='zh' else 28.8
            line_height=math.floor(line_height*64)/64
            lines=wrapped(content_text,font,size,410)
            height=len(lines)*line_height
            metrics=ImageFont.truetype(str(UI/font),size);ascender,descender=metrics.getmetrics()
            label_name=f'{name}_{kind}_{index}_{lang}'
            corpus=content_text+(' ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789%·°—/:.-' if kind=='device' else '')
            if kind=='device':
                for line_index,line in enumerate(lines):
                    line_name=label_name+'_line_'+str(line_index)
                    label(line_name,0,round(y+line_index*line_height)-2,410,round(line_height),line,size,'#34392f',copy[lang],font=font,bind=line_name,bind_target='text',font_charset=corpus,text_align='left')
            else:
                # Individual native font runs match the HTML's Arial-first
                # fallback: Latin digits/punctuation use Liberation Sans,
                # while CJK glyphs use Noto. Every baseline is rounded once.
                shift=(-3 if lang=='zh' else 3) if kind=='heading' else (6 if lang=='zh' else 7) if kind=='small' else 0 if lang=='zh' else 3
                for line_index,line in enumerate(lines):
                    x=0
                    for run_index,(run_font,run_text) in enumerate(text_runs(line,font)):
                        run_metrics=ImageFont.truetype(str(UI/run_font),size);run_ascent,run_descent=run_metrics.getmetrics()
                        run_name=label_name if line_index==0 and run_index==0 else f'{label_name}_line_{line_index}_run_{run_index}'
                        width=run_metrics.getlength(run_text)
                        label(run_name,round(x),round(y+line_index*line_height)+shift+ascender-run_ascent,410-round(x),run_ascent+run_descent,run_text,size,'#4f6340' if kind=='heading' else '#8c987e' if kind=='small' else '#34392f',copy[lang],font=run_font,text_align='left',text_vertical_align='top')
                        x+=width
            detail_defaults[name][lang].append({'name':label_name,'text':'\n'.join(lines),'y':round(y),'height':round(height)})
            y+=height+(13 if kind=='heading' else 18 if kind!='small' else 0)
        contract['scroll'][name]['height'][lang]=round(y)
        contract['scroll'][name]['height'][lang+'_max_scroll']=max(0,round(y)-345)

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
contract['detail_defaults']=detail_defaults

# Later paint order must leave labels above surfaces. Locale groups and role
# copy groups were declared before their shapes so move complete group trees
# behind neither the buttons nor the Canvas targets. A stable topological
# traversal also puts parents before children after home-label relocation.
for page_name in ('home','role_page','settings_page','guide_page','info_page','status_page','settings_content'):
    page_index=next(i for i,obj in enumerate(objects) if obj['name']==page_name)
    children=[i for i,obj in enumerate(objects) if obj['parent']==page_index]
    text_children=[i for i in children if any(i in pair.values() for pair in (home_text,role_text,settings_head,settings_text,status_text)) or objects[i]['name'].endswith(('_header_zh','_header_en'))]
    for i in text_children:objects[i]['_last']=True

for obj in objects:
    if obj['type']=='button':
        obj['disabled_color']='#f1efe8'; obj['disabled_opacity']=145
    if obj['name'] in ('volume_knob','volume_fill','richness_knob','richness_fill'):
        y=obj['y']; obj['y']={'default':y,'min':0,'max':180}
    if obj['name'] in ('record_surface','transport') or obj['name'].startswith('step_'):
        obj['bind']=obj['name']+'_color'; obj['bind_target']='color'
    if obj['name'] in ('record_icon','play_icon'):
        obj['hidden']=True
# Fill and progress colors follow the selected/committed role.
for obj in objects:
    if obj['name'].startswith('info_device_2_'):
        obj['font_charset']=obj.get('font_charset',obj.get('text',''))+'ABC0123456789%'
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
