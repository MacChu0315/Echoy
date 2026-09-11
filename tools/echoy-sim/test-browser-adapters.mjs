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
 const feed=(generation,sequence,sample)=>pcm.port.onmessage({data:{generation,sequence,pcm:new Int16Array(19200).fill(sample).buffer}});
 feed(1,100,1111);
 pcm.port.onmessage({data:{clear:true}});
 feed(2,1,2222);
 feed(1,101,3333);
 const output=[new Float32Array(9601),new Float32Array(9601)];
 pcm.process([], [output]);
 assert.equal(output[0][0],2222/32768);
 assert.equal(output[0][9600],0,'old generation cannot return after reset');
});

function application(fetch,clock={}){
 const callbacks={};
 const node={addEventListener(){}};
 const root={EchoyI18n:{apply(){}},duijuGuide:{targets(){return [];}},DUIJU_ROLES:{},addEventListener:(name,callback)=>{callbacks[name]=callback;}};
 const document={getElementById:()=>node,querySelectorAll:()=>[],addEventListener:(name,callback)=>{callbacks[name]=callback;},hidden:false};
 const context=vm.createContext({window:root,document,fetch,setInterval,clearInterval,setTimeout,clearTimeout,AbortController,performance,...clock});
 let source=fs.readFileSync(here+'/firmware.js','utf8');
 assert.equal(source.split('root.duiju=new App();').length,2);
 source=source.replace('root.duiju=new App();','root.adapterClasses={App,Frames,PCMOutput};');
 vm.runInContext(source,context);
 const app=Object.create(root.adapterClasses.App.prototype),frames=[],audio=[];
 Object.assign(app,{pending:Promise.resolve(),queueSize:0,busy:false,connected:true,connectionGeneration:1,lastSequence:0,lastGeneration:0,
  view:{frames:async answer=>frames.push(answer)},audio:{feed:answer=>audio.push(answer),stop(){}},toast(){},locale(){},update(answer){this.answer=answer;}});
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

// Exercise the shipped, unchanged observing guide with literal device
// observations. Advancing the guide is allowed even when a step is not done.
const html=fs.readFileSync(here+'/../../reference/Echoy_v5.0.3.html','utf8');
const guideStart=html.indexOf('class DuijuGuide{');
const guideEnd=html.indexOf('root.DuijuGuide=DuijuGuide;',guideStart);
assert(guideStart>0&&guideEnd>guideStart);
const Guide=vm.runInNewContext('('+html.slice(guideStart,guideEnd).trim()+')');
function observingGuide(step){
 const devices={A:{id:'A',present:true,transport:'paused',linkOnline:true,activity:.58,role:'pulse',phrase:1},
  B:{id:'B',present:true,transport:'paused',linkOnline:true,activity:.56,role:'voice',phrase:1},
  C:{id:'C',present:false,transport:'paused',linkOnline:true,activity:.48,role:'dust',phrase:1}};
 const syncing=new Set();const engine={devices,committedEdges:{},contactInput:{},pending:{phrases:{}},syncFor:id=>syncing.has(id)};
 const guide=Object.create(Guide.prototype);Object.assign(guide,{app:{engine,audio:{playing:true}},step,pair:['A','B'],orientation:'A>B'});guide.initial=guide.snapshot();
 const edge=(left,right)=>{engine.committedEdges[left+'>'+right]=true;engine.contactInput[left+'>'+right]={left,right,state:'connected'};};
 return {guide,engine,devices,syncing,edge};
}
test('Guide first step requires both local play intents and unlocked audio',()=>{
 const {guide,devices}=observingGuide(0);assert.equal(guide.evaluate().done,false);
 devices.A.transport='playing';assert.equal(guide.evaluate().done,false);
 devices.B.transport='playing';assert.equal(guide.evaluate().done,true);
 guide.app.audio.playing=false;assert.equal(guide.evaluate().done,false);
});
test('Guide connection waits for the committed, present, synchronized edge',()=>{
 const {guide,engine,syncing,edge}=observingGuide(1);assert.equal(guide.evaluate().done,false);
 engine.contactInput['A>B']={left:'A',right:'B',state:'connected'};assert.equal(guide.evaluate().done,false);
 edge('A','B');syncing.add('B');assert.equal(guide.evaluate().done,false);
 syncing.clear();assert.equal(guide.evaluate().done,true);assert.equal(guide.orientation,'A>B');
});
test('Guide swap observes reversed committed direction, not shell movement alone',()=>{
 const {guide,engine,edge}=observingGuide(2);edge('A','B');assert.equal(guide.evaluate().done,false);
 engine.contactInput={'B>A':{left:'B',right:'A',state:'connected'}};assert.equal(guide.evaluate().done,false);
 edge('B','A');assert.equal(guide.evaluate().done,true);
});
test('Guide shaping observes activity independently of volume',()=>{
 const {guide,devices}=observingGuide(3);devices.A.volume=1;assert.equal(guide.evaluate().done,false);
 devices.A.activity=.60;assert.equal(guide.evaluate().done,false);
 devices.A.activity=.64;assert.equal(guide.evaluate().done,true);
});
test('Guide phrase requires a committed phrase in the same role',()=>{
 const {guide,engine,devices}=observingGuide(4);engine.pending.phrases.A={value:2};assert.equal(guide.evaluate().done,false);
 devices.A.role='harmony';devices.A.phrase=2;assert.equal(guide.evaluate().done,false);
 devices.A.role='pulse';assert.equal(guide.evaluate().done,true);
});
test('Guide third tile requires both edges and all three local play intents; faults block completion',()=>{
 const {guide,devices,edge}=observingGuide(5);devices.A.transport=devices.B.transport='playing';edge('A','B');assert.equal(guide.evaluate().done,false);
 devices.C.present=true;edge('B','C');assert.equal(guide.evaluate().done,false);
 devices.C.transport='playing';assert.equal(guide.evaluate().done,true);
 devices.B.loading=true;const state=guide.evaluate();assert.equal(state.done,false);assert.equal(state.warning,true);
});

// Only HTTP latency/time and browser I/O are controlled; execute shipped start,
// timer, send, publication identity, and disconnect/reconnect logic unchanged.
for(const latency of [60,126])test(`App.start accounts for elapsed wall time with ${latency} ms responses`,async()=>{
 let wall=0,simulated=0,sequence=0,inflight=0,maxInflight=0,maxQueue=0,intervals=[],responses=[];
 const calls=[];
 const clock={performance:{now:()=>wall},setInterval:(fn,ms)=>{intervals.push({fn,ms,next:wall+ms});return intervals.length;},clearInterval(){}};
 const state=application((_url,options)=>{
  const request=JSON.parse(options.body);calls.push(request);inflight++;maxInflight=Math.max(maxInflight,inflight);
  return new Promise(resolve=>responses.push({at:wall+latency,finish(){
   if(request.op==='tick')simulated+=request.ms;
   inflight--;resolve({ok:true,json:async()=>({generation:1,sequence:++sequence,time_us:simulated*1000,devices:[]})});
  }}));
 },clock);
 const {app,frames,audio}=state;
 async function advance(ms){for(let i=0;i<ms;i++){
  wall++;for(const t of intervals)if(wall>=t.next){t.next+=t.ms;t.fn();}
  for(const r of responses.filter(r=>r.at<=wall))r.finish();responses=responses.filter(r=>r.at>wall);
  await turn();maxQueue=Math.max(maxQueue,app.queueSize);
 }}
 const started=app.start();await turn();await advance(latency*2+1);await started;
 const start=wall;await advance(1200);
 assert(simulated>=wall-start-latency-50,`advanced ${simulated} ms in ${wall-start} ms`);
 assert(simulated<=wall-start+1);assert.equal(maxInflight,1);assert(maxQueue<=2);
 assert(calls.filter(c=>c.op==='tick').every(c=>c.ms>0&&c.ms<=250),'bounded catch-up chunk');
 assert.deepEqual(frames.map(a=>[a.generation,a.sequence]),audio.map(a=>[a.generation,a.sequence]));
 app.disconnect();await advance(200);const offlineTime=simulated,offlineCalls=calls.length;
 await advance(500);assert.equal(simulated,offlineTime);assert.equal(calls.length,offlineCalls);
 const reconnected=app.reconnect();await advance(latency+1);await reconnected;
 const before=simulated;await advance(600);
 assert(simulated-before<=601,'reconnect never replays the offline elapsed time');
 assert(simulated-before>=600-latency-50);app.disconnect();await advance(200);
});

test('PCM absorbs 126 ms response spacing without recurring silence and bounds samples',()=>{
 let Processor;
 vm.runInNewContext(fs.readFileSync(here+'/pcm-worklet.js','utf8'),{AudioWorkletProcessor:class{constructor(){this.port={};}},registerProcessor:(_name,type)=>{Processor=type;},Int16Array});
 const pcm=new Processor();let sequence=0,started=false;
 const feed=ms=>pcm.port.onmessage({data:{generation:1,sequence:++sequence,pcm:new Int16Array(ms*48*2).fill(1000).buffer}});
 for(let time=0;time<1500;time++){
  if(time===166)feed(40);else if(time>=292&&(time-292)%126===0)feed(126);
  const output=[new Float32Array(48),new Float32Array(48)];pcm.process([], [output]);
  if(output[0].some(x=>x!==0))started=true;
  if(started)assert(output[0].every(x=>x!==0),`unexpected gap at ${time} ms`);
 }
 assert(started);feed(5000);
 assert(pcm.queue.reduce((n,p)=>n+p.length,0)<=48000*2,'queue bounds samples, including an oversized publication');
});
