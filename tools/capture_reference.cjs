#!/usr/bin/env node
// Read the frozen HTML without modifying it; select deterministic, paused states.
const {chromium} = require('playwright');
const fs = require('fs');
const path = require('path');
const {pathToFileURL} = require('url');
const root = path.resolve(__dirname, '..');
const out = path.resolve(process.argv[2] || path.join(root, 'output/reference-full'));
(async () => {
  fs.mkdirSync(out, {recursive:true});
  const browser = await chromium.launch({headless:true, executablePath:process.env.CHROME_PATH || '/opt/google/chrome/chrome'});
  try {
    const page = await browser.newPage({viewport:{width:1800,height:1600},deviceScaleFactor:1});
    await page.goto(pathToFileURL(path.join(root,'reference/echoy-arrange-B-v3.html')).href);
    await page.evaluate(() => document.fonts.ready);
    const states = ['pulse','voice','dust','harmony'].flatMap(role=>[
      {name:role+'-home',role,phrase:3,route:'home',lang:'zh-CN'},
      {name:'gallery-'+role+'-3',role,phrase:3,route:'role',lang:'zh-CN'}]);
    for(const lang of ['zh-CN','en']) for(const route of ['settings','guide','info','status']) {
      states.push({name:route+'-'+lang,role:'pulse',phrase:3,route,lang});
    }
    for(const state of states) {
      await page.evaluate(({role,phrase,route,lang})=>{
        const seed=[5,7,10,14].map(step=>({step,kind:'kick',vel:.74,dur:.22,origin:'you'}));
        const d=arrange.tile('A'),u=arrange.ui.A;
        Object.assign(d,{ownSeed:seed,role,phrase,density:.5,loop:false,present:true});
        d.memories={pulse:3,voice:3,dust:3,harmony:3};
        d.current={...RoleAccompaniment.compose(seed,role,phrase,.5),source:null,rootSeed:seed};
        Object.assign(u,{route:route==='status'?'home':route,fault:route==='status',preview:null,state:'idle',pulseAt:-1000,key:''});
        arrange.now=()=>100;
        arrange.lang=lang;document.documentElement.lang=lang;
        arrange.renderTile('A');arrange.update('A');arrange.drawGallery('A');
      },state);
      await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
      const box=await page.locator('[data-tile="A"] .screen').boundingBox();
      await page.screenshot({path:path.join(out,state.name+'.png'),clip:{x:Math.round(box.x),y:Math.round(box.y),width:480,height:480}});
    }
    fs.writeFileSync(path.join(out,'states.json'),JSON.stringify({seed:'44a0',density:.5,paused:true,states},null,2)+'\n');
    console.log(`Captured ${states.length} frozen HTML states at 480x480`);
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
