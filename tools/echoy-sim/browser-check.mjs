#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Real Chrome keyboard, mouse and touch input. No browser product-state writes.
import fs from 'node:fs';
import path from 'node:path';
import {spawn,spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
const root=path.resolve(import.meta.dirname,'../..'), out=path.join(root,'output/sim');
const url=process.argv[2]||'http://127.0.0.1:8879';
// The title pixels isolate the locale request from animation. The independent
// visual suite checks this same native fixture against the pinned HTML.
const titleExpected={};
for(const locale of ['zh-CN','en']){
 const file=path.join(out,'browser-title-'+locale+'.ppm');
 const result=spawnSync(path.join(root,'build-host/echoy_ui_runner'),[locale+'/perform-harmony',file]);
 assert.equal(result.status,0,result.stderr?.toString());
 const ppm=fs.readFileSync(file),header=Buffer.from('P6\n480 480\n255\n');assert(ppm.subarray(0,header.length).equals(header));
 const data=[];for(let y=35;y<91;y++)for(let x=35;x<245;x++){const at=header.length+(y*480+x)*3;data.push(ppm[at],ppm[at+1],ppm[at+2],255);}
 titleExpected[locale]=Buffer.from(data).toString('base64');
}
const titleExpression=`(async()=>{const img=document.querySelector('.screen[data-device=A] img');await img.decode();const canvas=document.createElement('canvas');canvas.width=210;canvas.height=56;const ctx=canvas.getContext('2d');ctx.drawImage(img,35,35,210,56,0,0,210,56);const bytes=ctx.getImageData(0,0,210,56).data;let text='';for(let i=0;i<bytes.length;i+=8192)text+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(text);})()`;
const profile=fs.mkdtempSync(path.join(out,'chrome-'));
const browser=spawn('/usr/bin/google-chrome',['--headless=new','--password-store=basic','--use-mock-keychain','--disable-background-networking','--no-proxy-server','--no-sandbox','--disable-dev-shm-usage','--disable-gpu','--autoplay-policy=no-user-gesture-required','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{stdio:'ignore'});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let socket;const checks=[],errors=[];
try{
 const file=path.join(profile,'DevToolsActivePort');for(let i=0;i<400&&!fs.existsSync(file);i++)await sleep(25);assert(fs.existsSync(file),'Chrome debug port');
 const port=fs.readFileSync(file,'utf8').split('\n')[0];const pages=await fetch(`http://127.0.0.1:${port}/json/list`).then(r=>r.json());socket=new WebSocket(pages.find(p=>p.type==='page').webSocketDebuggerUrl);
 await new Promise((r,j)=>{socket.onopen=r;socket.onerror=j;});let serial=0;const pending=new Map();
 socket.onmessage=({data})=>{const m=JSON.parse(data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result);}};
 const send=(method,params={})=>new Promise((resolve,reject)=>{const id=++serial;const timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timeout: '+method));},20000);pending.set(id,{resolve:value=>{clearTimeout(timer);resolve(value);},reject:error=>{clearTimeout(timer);reject(error);}});socket.send(JSON.stringify({id,method,params}));});
 const evaluate=async expression=>{const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
 const waitFor=async expression=>{for(let i=0;i<160;i++){if(await evaluate(expression))return;await sleep(100);}throw Error('Timeout '+expression);};
 const snapshot=async()=>{const dom=await send('DOMSnapshot.captureSnapshot',{computedStyles:[]});fs.writeFileSync(path.join(out,'browser-dom.json'),JSON.stringify(dom));};
 const center=async selector=>{await evaluate(`document.querySelector(${JSON.stringify(selector)}).scrollIntoView({block:'center',inline:'center'})`);return evaluate(`(()=>{const r=document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2}})()`);};
 const clickAt=async(x,y)=>{await send('Input.dispatchMouseEvent',{type:'mousePressed',x,y,button:'left',clickCount:1});await send('Input.dispatchMouseEvent',{type:'mouseReleased',x,y,button:'left',clickCount:1});await sleep(150);};
 const click=async selector=>{const p=await center(selector);await clickAt(p.x,p.y);};
 const screen=async(id,x,y)=>{const r=await evaluate(`(()=>{const e=document.querySelector('.screen[data-device="${id}"]');e.scrollIntoView({block:'center',inline:'center'});const r=e.getBoundingClientRect();return{x:r.x,y:r.y}})()`);await clickAt(r.x+x,r.y+y);};
 const record=(name,pass)=>{assert(pass,name);checks.push(name);console.log(name);};
 await send('Runtime.enable');await send('Page.enable');await send('Emulation.setDeviceMetricsOverride',{width:1600,height:1300,deviceScaleFactor:1,mobile:false});
 await send('Page.navigate',{url});await waitFor('window.echoy?.lastSequence>0');await snapshot();await click('#lab summary');await click('#reset');await waitFor('Object.values(echoy.engine.devices).every(d=>d.transport==="paused")');await click('#lab summary');record('No JavaScript product engine or synthesizer',await evaluate('!window.DuijuEngine&&!window.DuijuSynth&&!window.DuijuView'));
 record('All product screens use LVGL image frames',await evaluate('[...document.querySelectorAll(".screen")].every(s=>s.querySelector("img").src.startsWith("data:image/png;base64,")&&!s.querySelector("canvas"))'));
 record('Frame and PCM revision agreement',await evaluate('[...document.querySelectorAll(".screen")].every(s=>s.dataset.revision===s.dataset.audioRevision)'));
 await click('#guide-start');await waitFor('window.echoyGuide.mode==="lesson"');
 record('Guide observes and does not auto play',await evaluate('Object.values(echoy.engine.devices).every(d=>d.transport==="paused")'));
 for(let i=0;i<6;i++)await click('#guide-next');record('All six guide steps and finish',await evaluate('echoyGuide.mode==="finish"'));
 await click('#guide-reopen');record('Guide reopen preserves transport',await evaluate('Object.values(echoy.engine.devices).every(d=>d.transport==="paused")'));await click('#guide-skip');
 await screen('A',330,425);await waitFor('echoy.engine.devices.A.transport==="playing"');record('Mouse touch reaches C transport',true);
 await screen('A',100,425);await screen('A',385,163);await waitFor('echoy.engine.devices.A.role==="harmony"');record('Role selection commits through LVGL hit test',true);
 await screen('A',60,55);
 await send('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:1});
 const r=await evaluate('(()=>{const r=document.querySelector(".screen[data-device=A]").getBoundingClientRect();return{x:r.x,y:r.y}})()');
 const before=await evaluate('echoy.engine.devices.A.activity');
 await send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:r.x+200,y:r.y+270,id:1}]});
 await send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:r.x+200,y:r.y+200,id:1}]});
 await send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});await sleep(500);
 record('Real touch changes C activity',await evaluate(`Math.abs(echoy.engine.devices.A.activity-${before})>.1`));
 await send('Emulation.setTouchEmulationEnabled',{enabled:false});
 for(const locale of ['en','zh-CN']){await click(`[data-language="${locale}"]`);await waitFor(`(async()=>await ${titleExpression}===${JSON.stringify(titleExpected[locale])})()`);record(locale+' locale reaches rendered LVGL title pixels',true);}
 for(let i=0;i<35&&!await evaluate(`document.activeElement.matches('[data-id="A"] .device-handle')`);i++){await send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});await send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});}const old=await evaluate('echoy.positions.A.x');await send('Input.dispatchKeyEvent',{type:'keyDown',key:'ArrowLeft',code:'ArrowLeft',windowsVirtualKeyCode:37});await send('Input.dispatchKeyEvent',{type:'keyUp',key:'ArrowLeft',code:'ArrowLeft',windowsVirtualKeyCode:37});await sleep(200);record('Keyboard shell movement',await evaluate(`echoy.positions.A.x===${old}-20`));

 const drag=async(from,to)=>{await send('Input.dispatchMouseEvent',{type:'mousePressed',...from,button:'left',clickCount:1});await send('Input.dispatchMouseEvent',{type:'mouseMoved',...to,button:'left',buttons:1});await sleep(100);};
 const drop=async to=>{await send('Input.dispatchMouseEvent',{type:'mouseReleased',...to,button:'left',clickCount:1});await sleep(300);};
 const shellTarget=async(x,y)=>evaluate(`(()=>{const r=document.querySelector('#stage-viewport').getBoundingClientRect(),v=document.querySelector('#stage-viewport');return{x:r.x+${x}+254-v.scrollLeft,y:r.y+${y}+18-v.scrollTop}})()`);
 let from=await center('[data-id="A"] .device-handle'),to=await shellTarget(130,56);await drag(from,to);
 record('Shell snap preview visible before drop',await evaluate('!document.querySelector("#snap-hint").hidden'));await drop(to);
 await waitFor('echoy.answer.contacts.some(e=>e[0]==="A"&&e[1]==="B")');record('Shell snap publishes contact only',true);
 const arranged=await evaluate('JSON.stringify(echoy.positions)');from=await center('[data-id="A"] .device-handle');to=await shellTarget(646,56);await drag(from,to);await drop(to);
 record('Collision cancels shell placement',await evaluate(`JSON.stringify(echoy.positions)===${JSON.stringify(arranged)}`));
 from=await center('[data-id="A"] .device-handle');await send('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:1});
 await send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{...from,id:2}]});await send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:from.x+70,y:from.y+30,id:2}]});await send('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});await sleep(300);
 record('Touch cancel restores shell position',await evaluate(`JSON.stringify(echoy.positions)===${JSON.stringify(arranged)}`));await send('Emulation.setTouchEmulationEnabled',{enabled:false});
 const view=await center('#stage-viewport');await send('Input.dispatchMouseEvent',{type:'mouseWheel',...view,deltaX:0,deltaY:600});await sleep(300);record('Desk wheel scroll',await evaluate('document.querySelector("#stage-viewport").scrollTop>100'));
 from=await center('[data-reserve="C"]');to=await shellTarget(72,610);await drag(from,to);await drop(to);await waitFor('echoy.engine.devices.C.present');record('Third shell boots from reserve',true);
 from=await center('[data-id="C"] .device-handle');to=await evaluate('(()=>{const r=document.querySelector("#reserve-dock").getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2}})()');await drag(from,to);await drop(to);await waitFor('!echoy.engine.devices.C.present');record('Shell stow powers off C',true);
 await click('#lab summary');await click('[data-scenario="loading"]');await waitFor('echoy.engine.devices.A.loading');record('Fault panel injects loading',true);await screen('A',220,403);await waitFor('!echoy.engine.devices.A.loading');record('Screen cancels loading',true);
 await click('#restore');record('Fault restore keeps play intent',await evaluate('echoy.engine.devices.A.transport==="playing"'));
 await evaluate('window.scrollTo(0,0)');const screenshot=await send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(path.join(out,'firmware-browser.png'),Buffer.from(screenshot.data,'base64'));
 record('No runtime exceptions',errors.length===0);fs.writeFileSync(path.join(out,'browser.json'),JSON.stringify({passed:true,checks,errors},null,2));console.log(JSON.stringify({passed:true,checks},null,2));
}catch(error){fs.writeFileSync(path.join(out,'browser-error.json'),JSON.stringify({error:String(error),errors},null,2));throw error;}
finally{socket?.close();browser.kill('SIGTERM');await new Promise(r=>browser.once('exit',r));fs.rmSync(profile,{recursive:true,force:true,maxRetries:5,retryDelay:100});}
