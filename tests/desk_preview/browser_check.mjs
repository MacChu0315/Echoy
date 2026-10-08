// Real pointer/keyboard desk checks. Run against an isolated silent desk.
// node browser_check.mjs URL /path/to/installed/playwright/index.mjs /tmp/output
import {mkdir} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';
const [url, modulePath, output='/tmp/echoy-desk-browser'] = process.argv.slice(2);
const {chromium}=await import(pathToFileURL(modulePath));
await mkdir(output,{recursive:true});
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',headless:true,args:['--no-sandbox']});
try {
  const page=await browser.newPage({viewport:{width:1780,height:1160}}), errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(url);
  await page.locator('[data-tile="A"]').waitFor({state:'visible'});
  const frame=await page.locator('[data-tile="A"] iframe').contentFrame();
  await frame.locator('#screen').waitFor({state:'visible'});
  await page.waitForFunction(()=>document.getElementById('audio-status').textContent.includes('时钟')||document.getElementById('audio-status').textContent.includes('不会发声'));
  const dimensions=await frame.locator('#screen').evaluate(c=>{const r=c.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};});
  assert.deepEqual(dimensions,{x:260,y:62,width:480,height:480},'Official screen must remain 1:1 inside crop');
  const commands=[],times=[];page.on('request',r=>{if(r.url().endsWith('/command')){commands.push(r.postDataJSON());times.push(performance.now());}});
  const stage=await page.locator('#stage').boundingBox(), shelf=await page.locator('[data-shelf="B"]').boundingBox();
  await page.mouse.move(shelf.x+shelf.width/2,shelf.y+shelf.height/2);await page.mouse.down();
  await page.mouse.move(stage.x+548+254,stage.y+22+20,{steps:15});await page.mouse.up();
  await page.locator('[data-tile="B"]').waitFor({state:'visible'});
  await page.waitForFunction(()=>document.querySelector('[data-tile="A"] .right').classList.contains('connected'));
  assert(commands.some(c=>c.action==='present'&&c.tile==='B'&&c.present));
  assert(commands.some(c=>c.action==='edges'&&c.edges.some(e=>e.left==='A'&&e.right==='B')));
  const loading=commands.findIndex(c=>c.action==='condition'&&c.tile==='B'&&c.condition==='loading');
  const normal=commands.findIndex(c=>c.action==='condition'&&c.tile==='B'&&c.condition==='normal');
  const connected=commands.findIndex(c=>c.action==='edges'&&c.edges.length===1);
  assert(loading>=0&&normal>loading&&connected>normal,'Restore must boot before confirming edges');
  assert(times[normal]-times[loading]>=200,'Boot must wait 220ms, allowing 20ms measurement tolerance');
  assert(times[connected]-times[normal]>=425,'Contact confirmation must wait 450ms');
  const a=await page.locator('[data-tile="A"]').boundingBox(),b=await page.locator('[data-tile="B"]').boundingBox();
  assert.equal(b.x-a.x,516);assert.equal(b.y,a.y);
  await page.screenshot({path:output+'/linked.png',fullPage:true});
  // Keyboard movement also goes through the frozen snap/collision geometry.
  await page.locator('[data-tile="B"] .device-handle').focus();
  await page.keyboard.press('ArrowDown');await page.keyboard.press('ArrowDown');
  // First 32px move snaps back, so detach using pointer beyond 44px.
  const handle=await page.locator('[data-tile="B"] .device-handle').boundingBox();
  await page.mouse.move(handle.x+150,handle.y+20);await page.mouse.down();
  await page.mouse.move(handle.x+150,handle.y+120,{steps:8});await page.mouse.up();
  await page.waitForFunction(()=>{const seam=document.querySelector('[data-tile="A"] .right');return !seam.classList.contains('connected')&&!seam.classList.contains('pending');});
  assert(commands.some(c=>c.action==='edges'&&c.edges.length===0));
  assert(!commands.some(c=>c.action==='pause_all'),'Splitting must not send pause');
  const removingB=page.waitForRequest(r=>r.url().endsWith('/command')&&r.postDataJSON().action==='present'&&r.postDataJSON().tile==='B'&&!r.postDataJSON().present);
  await page.locator('[data-remove="B"]').click();await removingB;
  await page.locator('[data-shelf="B"]').waitFor({state:'visible'});
  assert(commands.some(c=>c.action==='present'&&c.tile==='B'&&!c.present));
  const addingC=page.waitForRequest(r=>r.url().endsWith('/command')&&r.postDataJSON().action==='present'&&r.postDataJSON().tile==='C'&&r.postDataJSON().present);
  await page.locator('[data-shelf="C"]').focus();await page.keyboard.press('Enter');await addingC;
  await page.locator('[data-tile="C"]').waitFor({state:'visible'});
  assert(commands.some(c=>c.action==='present'&&c.tile==='C'&&c.present));
  assert.equal(errors.length,0,errors.join('\n'));
  console.log(JSON.stringify({passed:7,dimensions,commands},null,2));
} finally {await browser.close();}
