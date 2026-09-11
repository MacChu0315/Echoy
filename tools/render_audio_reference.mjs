#!/usr/bin/env node
// Real Chromium OfflineAudioContext rendering of the pinned, unchanged synth.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
const dir=path.resolve(process.argv[2]||'output/generated/audio');
const html=fs.readFileSync('reference/Echoy_v5.0.3.html','utf8');
const hash=crypto.createHash('sha256').update(html).digest('hex');
if(hash!=='d23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5')throw Error('reference hash changed');
const begin=html.lastIndexOf('(function(root)',html.indexOf('class DuijuSynth'));
const end=html.indexOf('})(window);',begin)+'})(window);'.length;
const source=html.slice(begin,end);
const profile=fs.mkdtempSync(path.join(os.tmpdir(),'echoy-audio-chrome-'));
const chrome=spawn(process.env.ECHOY_CHROME||'/usr/bin/google-chrome',['--headless=new','--no-sandbox','--disable-dev-shm-usage','--disable-gpu','--disable-background-networking','--disable-component-update','--disable-sync','--no-first-run','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{stdio:['ignore','ignore','pipe']});
let stderr='';chrome.stderr.on('data',b=>stderr+=b);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));let socket;
try {
 let port;
 for(let i=0;i<200;i++){if(fs.existsSync(path.join(profile,'DevToolsActivePort'))){port=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0];break;}if(chrome.exitCode!==null)throw Error(stderr);await sleep(50);}
 if(!port)throw Error('Chrome CDP timeout: '+stderr);
 const tabs=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
 socket=new WebSocket(tabs.find(tab=>tab.type==='page').webSocketDebuggerUrl);await new Promise((r,j)=>{socket.onopen=r;socket.onerror=j;});
 let serial=0;const pending=new Map();socket.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result);}};
 const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
 const evalJS=async expression=>{const r=await call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
 // Pin a fully committed document before installing the synth. Chrome's
 // initial about:blank navigation can otherwise clear the injected class.
 await call('Page.enable');
 await call('Page.navigate',{url:'data:text/html,<title>echoy-audio-reference</title>'});
 let ready=false;
 for(let i=0;i<200;i++){
  ready=await evalJS("document.title==='echoy-audio-reference'&&document.readyState==='complete'");
  if(ready)break;
  await sleep(50);
 }
 if(!ready)throw Error('Chrome audio document readiness timeout');
 await evalJS(source);
 if(!await evalJS("typeof window.DuijuSynth==='function'"))throw Error('Synth installation failed');
 const version=await call('Browser.getVersion');
 const records=[];
 for(const name of fs.readdirSync(dir).filter(x=>x.endsWith('.json')&&!x.startsWith('reference-')).sort()){
  const spec=JSON.parse(fs.readFileSync(path.join(dir,name)));if(!spec.events)continue;
  // Both the original final compressor and an explicitly bypassed measurement
  // path are saved; only the latter isolates note/filter migration differences.
  for(const bypass of [false,true]){
   const data=await evalJS(`(async()=>{const spec=${JSON.stringify(spec)},ctx=new OfflineAudioContext(2,spec.frames,spec.sampleRate),synth=new DuijuSynth(ctx);if(${bypass}){synth.master.disconnect();synth.master.connect(ctx.destination);}for(const e of spec.events)synth.note('A',e,e.when,0);const b=await ctx.startRendering(),x=b.getChannelData(0);let text='';const bytes=new Uint8Array(x.buffer);for(let i=0;i<bytes.length;i+=8192)text+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(text);})()`);
   const filename=name.replace('.json',bypass?'.web-dry.f32':'.web.f32');fs.writeFileSync(path.join(dir,filename),Buffer.from(data,'base64'));records.push(filename);
  }
  console.log(name);
 }
 fs.writeFileSync(path.join(dir,'reference-provenance.json'),JSON.stringify({sourceSha256:hash,browser:version,rate:48000,channel:'left; centered identical stereo',originalCompressor:true,additionalBypassedCompressor:true,files:records},null,2)+'\n');
} finally {socket?.close();chrome.kill();await new Promise(r=>{if(chrome.exitCode!==null||chrome.signalCode!==null)r();else chrome.once('exit',r);});fs.rmSync(profile,{recursive:true,force:true,maxRetries:5,retryDelay:100});}
