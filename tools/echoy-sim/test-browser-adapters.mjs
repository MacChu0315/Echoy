#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
// Execute the shipped adapters in isolated JS contexts with controlled I/O.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {test} from 'node:test';
const here=import.meta.dirname;
const turn=()=>new Promise(resolve=>setImmediate(resolve));

test('PCM accepts reset generation at sequence 1 and rejects late old generation',()=>{
 let Processor;
 const context=vm.createContext({AudioWorkletProcessor:class{constructor(){this.port={};}},registerProcessor:(_name,type)=>{Processor=type;},Int16Array});
 vm.runInContext(fs.readFileSync(here+'/pcm-worklet.js','utf8'),context);
 const pcm=new Processor();
 const feed=(generation,sequence,sample)=>pcm.port.onmessage({data:{generation,sequence,pcm:new Int16Array([sample,sample]).buffer}});
 feed(1,100,1111);
 pcm.port.onmessage({data:{clear:true}});
 feed(2,1,2222);
 feed(1,101,3333);
 const output=[new Float32Array(2),new Float32Array(2)];
 pcm.process([], [output]);
 assert.equal(output[0][0],2222/32768);
 assert.equal(output[0][1],0,'old generation cannot return after reset');
});

function application(fetch){
 const callbacks={};
 const node={addEventListener(){}};
 const root={EchoyI18n:{},DUIJU_ROLES:{},addEventListener:(name,callback)=>{callbacks[name]=callback;}};
 const document={getElementById:()=>node,querySelectorAll:()=>[],addEventListener:(name,callback)=>{callbacks[name]=callback;},hidden:false};
 const context=vm.createContext({window:root,document,fetch,clearInterval,setTimeout,clearTimeout,AbortController});
 let source=fs.readFileSync(here+'/firmware.js','utf8');
 assert.equal(source.split('root.duiju=new App();').length,2);
 source=source.replace('root.duiju=new App();','root.adapterClasses={App,Frames,PCMOutput};');
 vm.runInContext(source,context);
 const app=Object.create(root.adapterClasses.App.prototype),frames=[],audio=[];
 Object.assign(app,{pending:Promise.resolve(),queueSize:0,busy:false,connected:true,connectionGeneration:1,lastSequence:0,lastGeneration:0,
  view:{frames:async answer=>frames.push(answer)},audio:{feed:answer=>audio.push(answer),stop(){}},toast(){},update(answer){this.answer=answer;}});
 app.bind();
 return {app,callbacks,document,frames,audio};
}
const response=sequence=>({ok:true,json:async()=>({sequence,generation:1,devices:[]})});
for(const reason of ['pagehide','hidden','network failure'])test(`${reason} invalidates unsent requests; explicit reconnect starts fresh`,async()=>{
 const calls=[];let finish;
 const state=application((_url,options)=>{
  calls.push(JSON.parse(options.body));
  if(calls.length===1)return new Promise((resolve,reject)=>{finish=()=>reason==='network failure'?reject(Error('disconnected')):resolve(response(1));});
  return Promise.resolve(response(calls.length));
 });
 const {app,callbacks,document,frames,audio}=state;
 app.send({op:'tick',ms:40});app.send({op:'touch',id:'A',phase:0,x:330,y:425});app.send({op:'touch',id:'A',phase:2,x:330,y:425});
 await turn();assert.equal(calls.length,1);
 if(reason==='pagehide')callbacks.pagehide();
 if(reason==='hidden'){document.hidden=true;callbacks.visibilitychange();}
 finish();await app.pending;
 assert.equal(calls.length,1,'only the request already accepted by the server can finish');
 assert.equal(app.connected,false,'late completion must not reconnect');
 assert.equal(frames.length,0);assert.equal(audio.length,0);
 await app.send({op:'tick',ms:40});assert.equal(calls.length,1,'disconnected send cannot inject');
 document.hidden=false;
 await app.reconnect();
 assert.equal(app.connected,true);assert.equal(calls.length,2);
 await app.send({op:'tick',ms:40});assert.equal(calls.length,3);
 assert.equal(app.queueSize,0);
});
