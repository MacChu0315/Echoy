#!/usr/bin/env node
// Capture the immutable prototype itself; generated evidence is ours, not ZIP provenance.
const {chromium} = require('playwright');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const root = path.resolve(__dirname, '..');
const argv=process.argv.slice(2);
const baselineIndex=argv.indexOf('--baseline');
const baseline=baselineIndex>=0?argv[baselineIndex+1]:'v4';
if(!['v3','v4'].includes(baseline))throw Error('--baseline must be v3 or v4');
const outputIndex=argv.indexOf('--output');
const positional=argv.find((x,i)=>!x.startsWith('--')&&argv[i-1]!=='--baseline'&&argv[i-1]!=='--output');
const out=path.resolve(outputIndex>=0?argv[outputIndex+1]:positional||path.join(root,'output/reference-'+baseline));
const source=path.join(root,baseline==='v4'?'reference/v4/echoy-arrange-B-v4.html':'reference/echoy-arrange-B-v3.html');
const bytes=fs.readFileSync(source),sha256=crypto.createHash('sha256').update(bytes).digest('hex');
(async()=>{
 fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH||'/usr/bin/google-chrome'});
 try{
  const page=await browser.newPage({viewport:{width:1800,height:1600},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.setContent(bytes.toString());await page.evaluate(()=>document.fonts.ready);
  const states=[];
  for(const lang of ['zh-CN','en'])for(const role of ['pulse','voice','dust','harmony'])for(const phrase of [1,2,3]){
   states.push({name:`home-${lang}-${role}-${phrase}`,role,phrase,route:'home',lang});
   if(phrase===3)states.push({name:`gallery-${lang}-${role}-3`,role,phrase,route:'role',lang});
  }
  for(const lang of ['zh-CN','en'])for(const route of ['settings','guide','info','status'])states.push({name:route+'-'+lang,role:'pulse',phrase:3,route,lang});
  const measurements=[];
  async function setState(state){
   await page.evaluate(({role,phrase,route,lang})=>{
    const seed=[5,7,10,14].map(step=>({step,kind:'kick',vel:.74,dur:.22,origin:'you'}));
    const d=arrange.tile('A'),u=arrange.ui.A;
    Object.assign(d,{ownSeed:seed,role,phrase,density:.5,loop:false,present:true});
    d.memories={pulse:3,voice:3,dust:3,harmony:3};d.memories[role]=phrase;
    d.current={...RoleAccompaniment.compose(seed,role,phrase,.5),source:null,rootSeed:seed};
    Object.assign(u,{route:route==='status'?'home':route,fault:route==='status'?'reference fault':'',preview:null,state:'ready',message:'',hit:null,key:''});
    arrange.now=()=>100;arrange.model.pending=null;arrange.compiling.clear();arrange.autoAfterCommit=null;
    arrange.lang=lang;document.documentElement.lang=lang;arrange.renderTile('A');arrange.update('A');arrange.drawGallery('A');
   },state);
   await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  }
  for(const state of states){
   await setState(state);
   const screen=page.locator('[data-tile="A"] .screen');
   const box=await screen.boundingBox();
   if(box.width!==480||box.height!==480)throw Error('Reference must remain 480x480');
   if(baseline==='v4'&&state.route==='home'){
    const geometry=await screen.evaluate(e=>{
     const s=e.getBoundingClientRect(),box=n=>{const r=n.getBoundingClientRect();return {x:r.x-s.x,y:r.y-s.y,w:r.width,h:r.height,right:r.right-s.x,cy:r.y+r.height/2-s.y,overflow:n.scrollWidth>n.clientWidth+1};};
     return {labels:[...e.querySelectorAll('.identity-role,.identity small')].map(box),battery:box(e.querySelector('.battery')),pad:box(e.querySelector('.pad')),strip:box(e.querySelector('.merge-strip')),buttons:[...e.querySelectorAll('.actions button:enabled')].map(box),state:e.querySelector('[data-slot="state"]').textContent,legacyHint:e.querySelector('.hint')!==null};
    });
    if(geometry.pad.y!==89||geometry.strip.w!==244||geometry.strip.h!==84||geometry.legacyHint||geometry.state!=='')throw Error('v4 home layout/status contract mismatch');
    if(geometry.labels.some(x=>x.overflow)||Math.abs(geometry.labels[0].cy-geometry.labels[1].cy)>=1||geometry.labels[1].right+12>=geometry.battery.x)throw Error('Reference header does not fit');
    if(geometry.buttons.some(x=>x.w<84||x.h<84))throw Error('Active button too small');
    measurements.push({state:state.name,...geometry});
   }
   await page.screenshot({path:path.join(out,state.name+'.png'),clip:{x:Math.round(box.x),y:Math.round(box.y),width:480,height:480}});
  }
  const accessibility=[];
  if(baseline==='v4'){
   await setState({role:'pulse',phrase:1,route:'home',lang:'zh-CN'});
   // Isolate reference focus/state behavior from audio output; no sound is played.
   await page.evaluate(()=>{arrange.audio.ensure=async()=>{};arrange.audio.volumeTile=()=>{};arrange.audio.recordGuide=()=>{};});
   const strip=page.locator('[data-screen="A"] .merge-strip');
   await strip.locator('.merge-record').focus();await page.keyboard.press('Enter');
   await page.waitForFunction(()=>arrange.ui.A.state==='countin');
   if(!await page.evaluate(()=>document.activeElement.matches('.merge-single')))throw Error('Focus did not follow Cancel');
   await page.keyboard.press('Enter');await page.waitForFunction(()=>arrange.ui.A.state==='ready');
   if(!await page.evaluate(()=>document.activeElement.matches('.merge-record')))throw Error('Focus did not return after cancel');
   accessibility.push('HTML keyboard Enter/focus follows active control; audio adapter stubbed for this UI-only assertion');
   await page.emulateMedia({reducedMotion:'reduce'});
   const duration=await strip.locator('.record-tint').evaluate(e=>getComputedStyle(e).transitionDuration);
   if(!duration.split(',').every(x=>parseFloat(x)===0))throw Error('Reduced motion still animates');
   accessibility.push('HTML prefers-reduced-motion removes morph travel');
  }
  if(errors.length)throw Error(errors.join('\n'));
  fs.writeFileSync(path.join(out,'states.json'),JSON.stringify({baseline,source,sha256,seed:'44a0',density:.5,paused:true,states,measurements,accessibility,scope:'Our fresh execution of immutable HTML; not native GSP keyboard/audio acceptance and not supplied ZIP test results'},null,2)+'\n');
  console.log(`Captured ${states.length} ${baseline} states; ${measurements.length} home header/layout checks; ${accessibility.length} HTML accessibility checks`);
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
