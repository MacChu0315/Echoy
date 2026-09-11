/* Product rules: deterministic phrases and committed relationships. No browser APIs. */
(function(root){'use strict';
const response=root.DuijuResponse||(typeof require==='function'?require('./response.js'):null);
const harmony=root.DuijuHarmony||(typeof require==='function'?require('./harmony.js'):null);
const ROLES=Object.freeze({
 pulse:{name:'PULSE',cn:'脉冲',color:'#d95535',desc:'敲击 · 重音 · 留白',init:.58,phrases:['重音','切分','留白']},
 voice:{name:'VOICE',cn:'声线',color:'#496da1',desc:'音高 · 短句 · 回应',init:.56,phrases:['短句','轻答','长线']},
 dust:{name:'DUST',cn:'微尘',color:'#7b8452',desc:'颗粒 · 空气 · 余韵',init:.48,phrases:['颗粒','点缀','余韵']},
 harmony:{name:'HARMONY',cn:'和声',color:'#aa7b3d',desc:'短奏 · 分解 · 接句',init:.50,phrases:['短和弦','分解','接句']}
});
const clamp=(v,a=0,b=1)=>Math.min(b,Math.max(a,Number.isFinite(v)?v:a));
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const edgeKeys=ids=>ids.slice(1).map((id,i)=>ids[i]+'>'+id);
const pending=()=>({roles:{},phrases:{},topology:null,activity:false});
const device=(id,role,present)=>({id,role,present,activity:ROLES[role].init,committedActivity:ROLES[role].init,memories:{pulse:.58,voice:.56,dust:.48,harmony:.50},phrase:1,phraseMemories:{pulse:1,voice:1,dust:1,harmony:2},volume:.70,battery:id==='A'?86:id==='B'?72:91,imu:0,wifi:true,moduleL:'none',moduleR:'none',muted:true,transport:'paused',fault:null,error:null,loading:false,rolePreparing:null});
class DuijuEngine{
 constructor(){
  this.devices={A:device('A','pulse',true),B:device('B','voice',true),C:device('C','dust',false)};
  this.order=['A','B'];this.seams={'A>B':true};this.peerOnline=true;this.clockId='session-clock-01';this.bpm=96;this.bar=0;this.pending=pending();this.revision=0;
 }
 get linked(){return Object.values(this.seams).some(Boolean);}
 queueRole(id,role){
  const d=this.devices[id];if(!d||!ROLES[role])return false;
  if(role===d.role){delete this.pending.roles[id];if(this.pending.phrases[id]?.role!==role)delete this.pending.phrases[id];}
  else {this.pending.roles[id]=role;if(this.pending.phrases[id]?.role!==role)delete this.pending.phrases[id];}
  d.error=null;return true;
 }
 queuePhrase(id,value){
  const d=this.devices[id];if(!d||!Number.isInteger(value)||value<1||value>3)return false;
  const role=this.pending.roles[id]||d.role;const base=role===d.role?d.phrase:d.phraseMemories[role];
  if(value===base)delete this.pending.phrases[id];else this.pending.phrases[id]={role,value};return true;
 }
 failRole(id,message){delete this.pending.roles[id];delete this.pending.phrases[id];this.devices[id].rolePreparing=null;this.devices[id].error=message;}
 invalidateTopology(){this.pending.topology=null;}
 queueTopology(ids,seams){
  if(!Array.isArray(ids)||!ids.length||ids.length>3||new Set(ids).size!==ids.length||ids.some(id=>!this.devices[id]))return false;
  const edges={};for(const key of edgeKeys(ids))edges[key]=seams[key]===true;
  this.pending.topology=same(ids,this.order)&&same(edges,this.seams)?null:{order:ids.slice(),seams:edges};return true;
 }
 queueOrder(ids){const active=this.linked;return this.queueTopology(ids,Object.fromEntries(edgeKeys(ids).map(k=>[k,active])));}
 queueLinked(value){const ids=this.pending.topology?.order||this.order;return this.queueTopology(ids,Object.fromEntries(edgeKeys(ids).map(k=>[k,!!value])));}
 setActivity(id,value){const d=this.devices[id];if(!d)return;d.activity=clamp(value);d.memories[d.role]=d.activity;this.pending.activity=Object.values(this.devices).some(x=>Math.abs(x.activity-x.committedActivity)>1e-8);}
 setVolume(id,value){if(this.devices[id])this.devices[id].volume=clamp(value);}
 pause(id){const d=this.devices[id];if(d){d.transport='paused';d.resumeAt='beat';d.muted=true;}}
 requestPlay(id,resumeAt='beat'){const d=this.devices[id];if(d?.present&&!d.fault&&!d.loading&&d.battery>0){d.transport='waiting';d.resumeAt=resumeAt==='bar'?'bar':'beat';d.muted=true;return true;}return false;}
 activateWaiting(isBar=true){const ids=[];for(const d of Object.values(this.devices))if(d.transport==='waiting'&&(isBar||d.resumeAt!=='bar')&&this.order.includes(d.id)&&d.present&&!d.fault&&!d.loading&&d.battery>0){d.transport='playing';d.muted=false;ids.push(d.id);}return ids;}
 hasPending(){return !!(Object.keys(this.pending.roles).length||Object.keys(this.pending.phrases).length||this.pending.topology||this.pending.activity);}
 commit(){
  const changed=this.hasPending();
  if(this.pending.topology){this.order=this.pending.topology.order.slice();this.seams={...this.pending.topology.seams};}
  for(const [id,role] of Object.entries(this.pending.roles)){
   const d=this.devices[id];d.memories[d.role]=d.activity;d.phraseMemories[d.role]=d.phrase;
   d.role=role;d.activity=d.memories[role];d.phrase=d.phraseMemories[role];d.rolePreparing=null;
  }
  for(const [id,{role,value}] of Object.entries(this.pending.phrases)){const d=this.devices[id];if(d.role===role){d.phrase=value;d.phraseMemories[role]=value;}}
  Object.values(this.devices).forEach(d=>d.committedActivity=d.activity);
  this.pending=pending();if(changed)this.revision++;return changed;
 }
 compose(bar=0){
  const result=[];let upstream=null,previous=null,counts={};
  for(const id of this.order){
   const d=this.devices[id],connected=this.peerOnline&&this.seams[previous+'>'+id]===true;
   if(!connected) {upstream=null;counts={};}
   if(!d.present){upstream=null;counts={};previous=id;continue;}
   const source=connected&&upstream?upstream:null;
   if(!source)counts={};const duplicate=counts[d.role]||0;counts[d.role]=duplicate+1;
   const events=this.phrase(d,source,bar,duplicate);
   const available=(!d.fault||d.fault==='audio')&&!d.loading&&d.battery>0;
   const outputAvailable=available&&!d.fault;
   const entry={id,role:d.role,source:source?source.id:null,events:outputAvailable&&!d.muted?events:[],motif:available?events:[],activity:d.committedActivity,phrase:d.phrase};
   result.push(entry);upstream=available&&entry.motif.length?entry:null;previous=id;
  }return result;
 }
 phrase(d,source,bar,duplicate){
  if(d.role==='harmony')return harmony.compose(d,source,bar,duplicate,this.bpm);
  if(source?.motif?.length)return response.compose(d,source,bar,duplicate,this.bpm);
  // Exact 0.3 solo vocabulary. Only responses change in 0.4.
  const a=d.committedActivity,notes=[62,69,65,72,67,74,69,65];let ev=[];
  if(d.role==='pulse'){
   ev=[{step:0,kind:'kick',vel:.88,note:38,dur:.24,accent:true},{step:8,kind:'kick',vel:.75,note:38,dur:.22,accent:true},
       {step:4,kind:'rim',vel:.66,note:50,dur:.10,accent:true},{step:12,kind:'rim',vel:.73,note:50,dur:.1,accent:true}];
   for(let s=2;s<16;s+=2)if(s%4!==0||a>.65)ev.push({step:s,kind:'hat',vel:.2+a*.22,note:84,dur:.05,accent:false});
   if(a>.7)ev.push({step:15,kind:'rim',vel:.36,note:53,dur:.08,accent:false});
  }else if(d.role==='voice'){
   ev=[0,3,7,10,14].slice(0,3+Math.floor(a*4)).map((step,i)=>({step,kind:'tone',vel:.52+(i%3===0?.15:0)+a*.12,note:notes[(i+(bar%4>=2?2:0))%notes.length]+(duplicate%2?12:0),dur:.32+a*.2,accent:true}));
   if(a>.82&&!ev.some(e=>e.step===15))ev.push({step:15,kind:'tone',vel:.32,note:77,dur:.13,accent:false});
  }else{
   ev=[1,6,11,15].slice(0,3+Math.floor(a*5)).map((step,i)=>({step,kind:'grain',vel:.32+a*.28,note:notes[(i+3+duplicate)%notes.length]+12,dur:.14+a*.2,accent:i%2===0}));
   if(a>.72)for(const step of [3,9,13])if(!ev.some(e=>e.step===step))ev.push({step,kind:'grain',vel:.25,note:86,dur:.13,accent:false});
  }
  if(d.phrase===2||d.phrase===3)ev=this.soloVariant(d,bar,duplicate,notes);
  const expression=a-ROLES[d.role].init;ev=ev.map(n=>({...n,expression,dur:n.dur*(1+expression*.45)}));
  if(a<.26)ev=ev.filter((e,i)=>e.accent&&i%2===0);
  const seen=new Set();return ev.sort((x,y)=>x.step-y.step).filter(e=>{const key=e.step+':'+e.kind;if(seen.has(key))return false;seen.add(key);e.vel=clamp(e.vel,.05,1);return true;}).slice(0,16);
 }
 soloVariant(d,bar,duplicate,notes){
  const a=d.committedActivity,v=d.phrase;let ev=[];
  if(d.role==='pulse'){
   ev=(v===2?[0,3,7,10,14]:[0,6,11]).map((step,i)=>({step,kind:i%3===0?'kick':'rim',vel:i%3===0?.78:.59,note:i%3===0?38:50,dur:i%3===0?.23:.09,accent:true}));
   if(a>.42&&v===2)for(const step of [2,9,15])if(!ev.some(n=>n.step===step))ev.push({step,kind:'hat',vel:.27,note:84,dur:.04,accent:false});
   if(a>.72&&v===3)ev.push({step:14,kind:'rim',vel:.31,note:53,dur:.08,accent:false});
  }else if(d.role==='voice'){
   ev=(v===2?[1,4,6,9,13]:[0,6,12]).slice(0,v===2?4+Math.floor(a*2):3+Math.floor(a*2)).map((step,i)=>({step,kind:'tone',vel:.53+(i===0?.12:0),note:notes[(i*(v===2?2:1)+(v===2?2:4))%notes.length]+(duplicate%2?12:0),dur:v===2?.16+a*.13:.45+a*.34,accent:true}));
  }else{
   ev=(v===2?[2,9,14]:[0,5,10]).slice(0,3+Math.floor(a*3)).map((step,i)=>({step,kind:'grain',vel:v===2?.38:.42,note:notes[(i+(v===2?1:4)+duplicate)%notes.length]+12,dur:v===2?.09+a*.08:.32+a*.3,accent:i%2===0}));
   if(v===2&&a>.76)ev.push({step:15,kind:'grain',vel:.22,note:86,dur:.1,accent:false});
  }return ev;
 }
 snapshot(){return JSON.parse(JSON.stringify({devices:this.devices,order:this.order,seams:this.seams,linked:this.linked,peerOnline:this.peerOnline,pending:this.pending,revision:this.revision,bpm:this.bpm,clockId:this.clockId}));}
}
root.DUIJU_ROLES=ROLES;root.DuijuEngine=DuijuEngine;
})(typeof window!=='undefined'?window:globalThis);
