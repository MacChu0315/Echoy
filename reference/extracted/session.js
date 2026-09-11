/* Product session model. Only IDs, contacts, protocol states and injected monotonic time.
 * No DOM, AudioContext, global desk coordinates or hardware APIs. Timing values are
 * explicit simulation assumptions, NOT measured radio/acoustic performance. */
(function(root){'use strict';
const R=root.DUIJU_ROLES,clone=x=>JSON.parse(JSON.stringify(x)),edgeKey=(l,r)=>l+'>'+r;
class DuijuSession extends root.DuijuEngine{
 constructor(now=0){
  super();this.time=now;this.order=['A','B','C'];this.seams={};this.clockId=null;this.sessions={};this.contactInput={};this.committedEdges={};this.merges={};this.arrangements={};this.moving={};this.events=[];this.serial=0;this.alignEvents=[];
  const uids={A:'tile-8f31',B:'tile-b472',C:'tile-d6a9'};
  for(const d of Object.values(this.devices)){Object.assign(d,{present:false,uid:uids[d.id],bootCount:0,bootId:null,sessionId:null,lifecycle:'off',linkOnline:true,linkLostAt:null,discovered:[],syncFail:false,clockVersion:0,motifAvailable:false});}
  this.powerOn('A',now);this.powerOn('B',now+.32);
 }
 emit(type,data={}){this.events.push({type,time:this.time,...data});if(this.events.length>60)this.events.shift();}
 freshSession(id,at,epoch=null){
  const d=this.devices[id],sid=`${d.uid}/${d.bootCount}/${++this.serial}`;
  const s={id:sid,leader:id,members:[id],epoch:epoch??at+.075,rate:1+({A:28,B:-41,C:63}[id])*1e-6,created:at,lastBar:-1};
  this.sessions[sid]=s;d.sessionId=sid;d.clockVersion++;return s;
 }
 leave(id,at){
  const d=this.devices[id],s=this.sessions[d.sessionId];if(s){s.members=s.members.filter(x=>x!==id);if(!s.members.length)delete this.sessions[s.id];else if(s.leader===id)s.leader=s.members.slice().sort((a,b)=>this.devices[a].uid.localeCompare(this.devices[b].uid))[0];}d.sessionId=null;
  for(const k of Object.keys(this.committedEdges))if(k.split('>').includes(id))delete this.committedEdges[k];
 }
 powerOn(id,at=this.time){
  const d=this.devices[id];if(!d||d.present||d.battery===0)return false;
  d.present=true;d.bootCount++;d.bootId=`${d.uid}/boot-${d.bootCount}`;d.lifecycle='booting';d.motifAvailable=false;d.readyAt=at+.38;d.sessionId=null;d.muted=true;d.transport='paused';d.linkLostAt=null;d.discovered=[];d.fault=null;d.error=null;d.loading=false;d.rolePreparing=null;d.linkOnline=true;this.revision++;this.emit('boot',{id});return true;
 }
 powerOff(id,at=this.time){
  const d=this.devices[id];if(!d)return;this.leave(id,at);d.present=false;d.lifecycle='off';d.muted=true;d.transport='paused';d.discovered=[];d.clockVersion++;delete this.pending.roles[id];delete this.pending.phrases[id];
  for(const k of Object.keys(this.contactInput))if(k.split('>').includes(id))delete this.contactInput[k];
  for(const k of Object.keys(this.merges))if(this.merges[k].ids.includes(id))delete this.merges[k];
  this.arrangements={};this.revision++;this.emit('off',{id});
 }
 setLink(id,online,at=this.time){const d=this.devices[id];if(!d)return;d.linkOnline=!!online;d.linkLostAt=online?null:at;this.emit(online?'link-restored':'link-lost',{id});}
 sessionFor(id){return this.sessions[this.devices[id]?.sessionId]||null;}
 phase(id,at=this.time){
  const s=this.sessionFor(id);if(!s)return{step:-1,fraction:0,bar:0,remaining:0,beatRemaining:0,total:0};
  const stepTime=60/this.bpm/4/s.rate,total=Math.max(0,(at-s.epoch)/stepTime),inBar=total%16;
  return{step:Math.floor(total)%16,fraction:inBar/16,bar:Math.floor(total/16),remaining:(16-inBar)*stepTime,beatRemaining:(4-total%4)*stepTime,total,stepTime};
 }
 nextBoundary(s,at,steps=16){const p=60/this.bpm/4/s.rate;return s.epoch+Math.max(0,Math.ceil((at-s.epoch)/p/steps))*steps*p;}
 requestPlay(id){const d=this.devices[id],s=this.sessionFor(id);if(!d?.present||!s||d.lifecycle==='booting'||d.loading||d.fault||d.battery===0)return false;d.transport='waiting';d.muted=true;d.playAt=this.nextBoundary(s,this.time+.025,4);d.playSession=s.id;return true;}
 pause(id){super.pause(id);if(this.devices[id])delete this.devices[id].playAt;}
 beginMove(id,at=this.time){this.moving[id]=at;this.arrangements={};for(const k of Object.keys(this.merges))if(this.merges[k].ids.includes(id))delete this.merges[k];this.emit('move',{id});}
 endMove(id){delete this.moving[id];}
 receiveContacts(list,at=this.time){
  const next={},leftUsed=new Set(),rightUsed=new Set();
  for(const e of list){const {left,right}=e;if(!this.devices[left]||!this.devices[right]||left===right||leftUsed.has(left)||rightUsed.has(right))continue;
   let cursor=right,cycle=false;for(let i=0;i<3;i++){const out=Object.values(next).find(x=>x.left===cursor);if(cursor===left){cycle=true;break;}if(!out)break;cursor=out.right;}if(cycle)continue;
   const k=edgeKey(left,right),state=e.state==='uncertain'?'uncertain':'connected',old=this.contactInput[k];
   next[k]={left,right,state,since:old?.state===state?old.since:at};leftUsed.add(left);rightUsed.add(right);
  }
  this.contactInput=next;this.arrangements={};
  // New contact evidence invalidates only candidates no longer justified by it.
  for(const k of Object.keys(this.merges)){const m=this.merges[k];if(!Object.values(next).some(e=>m.ids.includes(e.left)&&m.ids.includes(e.right)&&e.state==='connected'))delete this.merges[k];}
  this.revision++;
 }
 contactAvailable(c){const a=this.devices[c.left],b=this.devices[c.right];return a?.present&&b?.present&&a.sessionId&&b.sessionId&&a.linkOnline&&b.linkOnline&&a.battery>0&&b.battery>0;}
 syncFor(id){return Object.values(this.merges).find(m=>m.ids.includes(id))||null;}
 commitMembers(ids){
  let changed=false;
  for(const id of ids){const d=this.devices[id],role=this.pending.roles[id];if(role){d.memories[d.role]=d.activity;d.phraseMemories[d.role]=d.phrase;d.role=role;d.activity=d.memories[role];d.phrase=d.phraseMemories[role];d.rolePreparing=null;delete this.pending.roles[id];changed=true;}
   const q=this.pending.phrases[id];if(q){if(q.role===d.role){d.phrase=q.value;d.phraseMemories[d.role]=q.value;changed=true;}delete this.pending.phrases[id];}
   if(Math.abs(d.activity-d.committedActivity)>1e-8){d.committedActivity=d.activity;changed=true;}
  }
  this.pending.activity=Object.values(this.devices).some(d=>d.present&&Math.abs(d.activity-d.committedActivity)>1e-8);
  if(changed){this.revision++;this.emit('parameters',{ids:ids.slice()});}return changed;
 }
 commitIdle(id){const s=this.sessionFor(id);if(s&&s.members.every(x=>this.devices[x].transport==='paused'))this.commitMembers(s.members);}
 tick(at){
  this.time=Math.max(this.time,at);
  for(const d of Object.values(this.devices)){
   if(d.present&&d.lifecycle==='booting'&&this.time>=d.readyAt){this.freshSession(d.id,d.readyAt);d.lifecycle='ready';this.emit('ready',{id:d.id});}
   if(d.present&&d.battery===0){this.powerOff(d.id,this.time);continue;}
   if(d.present&&d.sessionId&&!d.linkOnline&&d.linkLostAt!==null&&this.time-d.linkLostAt>=1.8&&!d.lostSplit){const phase=this.phase(d.id),old=this.sessionFor(d.id),period=60/this.bpm/4;const epoch=this.time-phase.total*period;this.leave(d.id,this.time);this.freshSession(d.id,this.time,epoch);d.lostSplit=true;this.emit('split',{id:d.id});}
   if(d.linkOnline)d.lostSplit=false;
   d.discovered=d.present&&d.linkOnline&&d.lifecycle!=='booting'?Object.values(this.devices).filter(p=>p.id!==d.id&&p.present&&p.linkOnline&&p.lifecycle==='ready'&&p.battery>0).map(p=>p.id):[];
  }
  this.updateMerges();this.updateArrangements();
  for(const s of Object.values(this.sessions)){
   const bar=this.phase(s.members[0]).bar;
   if(bar!==s.lastBar){
    s.lastBar=bar;
    // A failed speaker does not erase a valid phrase. Genuine data availability
    // is latched at the boundary so a follower never changes motif mid-bar.
    for(const id of s.members){const d=this.devices[id];d.motifAvailable=(!d.fault||d.fault==='audio')&&!d.loading&&d.battery>0;}
    this.commitMembers(s.members);
   }
   for(const id of s.members){const d=this.devices[id];if(d.transport==='waiting'){
    if(d.playSession!==s.id){d.playAt=this.nextBoundary(s,this.time+.025,4);d.playSession=s.id;}
    if(this.time>=d.playAt&&!d.fault&&!d.loading&&d.battery>0){d.transport='playing';d.muted=false;this.emit('playing',{id});}
   }}
  }
 }
 updateMerges(){
  const adj={};
  for(const c of Object.values(this.contactInput)){
   if(c.state!=='connected'||this.time-c.since<.45||!this.contactAvailable(c)||this.moving[c.left]!==undefined||this.moving[c.right]!==undefined)continue;
   const a=this.devices[c.left].sessionId,b=this.devices[c.right].sessionId;if(a===b)continue;(adj[a]??=new Set()).add(b);(adj[b]??=new Set()).add(a);
  }
  const seen=new Set(),active=new Set();
  for(const sid of Object.keys(adj)){
   if(seen.has(sid))continue;const group=[],stack=[sid];while(stack.length){const x=stack.pop();if(seen.has(x))continue;seen.add(x);group.push(x);for(const n of adj[x]||[])stack.push(n);}
   const ss=group.map(x=>this.sessions[x]).filter(Boolean);if(ss.length<2)continue;
   const key=group.sort().join('|'),ids=ss.flatMap(s=>s.members);active.add(key);
   let m=this.merges[key];
   if(!m){ss.sort((a,b)=>b.members.length-a.members.length||this.devices[a.leader].uid.localeCompare(this.devices[b.leader].uid));m=this.merges[key]={key,ids,sessionIds:group,reference:ss[0].id,start:this.time,readyAt:this.time+1.05,stage:'syncing',applyAt:null};this.emit('sync-start',{ids:ids.slice()});}
   if(ids.some(id=>this.devices[id].syncFail)){m.stage='failed';m.applyAt=null;continue;}
   if(m.stage==='failed'){m.start=this.time;m.readyAt=this.time+1.05;m.stage='syncing';}
   if(this.time>=m.readyAt&&m.applyAt===null){m.stage='waiting';m.applyAt=this.nextBoundary(this.sessions[m.reference],this.time+.14,16);}
   if(m.applyAt!==null&&this.time>=m.applyAt){
    const ref=this.sessions[m.reference];if(!ref)continue;const moved=[];
    for(const old of m.sessionIds){const s=this.sessions[old];if(!s||old===ref.id)continue;
     for(const id of s.members){const d=this.devices[id];d.sessionId=ref.id;d.clockVersion++;moved.push(id);if(!ref.members.includes(id))ref.members.push(id);}
     delete this.sessions[old];delete this.arrangements[old];
    }
    this.alignEvents.push({ids:moved,time:this.time});
    for(const c of Object.values(this.contactInput))if(this.contactAvailable(c)&&c.state==='connected'&&this.devices[c.left].sessionId===ref.id&&this.devices[c.right].sessionId===ref.id&&this.moving[c.left]===undefined&&this.moving[c.right]===undefined)this.committedEdges[edgeKey(c.left,c.right)]=true;
    this.revision++;this.emit('joined',{ids:ref.members.slice(),leader:ref.leader});delete this.merges[key];
   }
  }
  for(const key of Object.keys(this.merges))if(!active.has(key))delete this.merges[key];
 }
 updateArrangements(){
  for(const s of Object.values(this.sessions)){
   const target={};
   for(const c of Object.values(this.contactInput)){
    const k=edgeKey(c.left,c.right);if(!s.members.includes(c.left)||!s.members.includes(c.right)||!this.contactAvailable(c))continue;
    const moving=this.moving[c.left]??this.moving[c.right];
    if(c.state==='connected'&&this.time-c.since>=.45&&moving===undefined)target[k]=true;
    else if(this.committedEdges[k]&&this.time-(moving??c.since)<1.8)target[k]=true;
   }
   const current=Object.fromEntries(Object.entries(this.committedEdges).filter(([k])=>k.split('>').every(id=>s.members.includes(id))));
   const signature=JSON.stringify(Object.keys(target).sort());
   if(signature===JSON.stringify(Object.keys(current).sort())){delete this.arrangements[s.id];continue;}
   let p=this.arrangements[s.id];
   if(!p||p.signature!==signature){p=this.arrangements[s.id]={signature,target,applyAt:this.nextBoundary(s,this.time+.04,16),ids:s.members.slice()};}
   if(this.time>=p.applyAt){for(const k of Object.keys(current))delete this.committedEdges[k];Object.assign(this.committedEdges,target);delete this.arrangements[s.id];this.revision++;this.emit('arranged',{ids:s.members.slice()});}
  }
  for(const k of Object.keys(this.committedEdges)){const [a,b]=k.split('>');if(!this.devices[a].present||!this.devices[b].present||this.devices[a].sessionId!==this.devices[b].sessionId)delete this.committedEdges[k];}
 }
 composeSession(sid,bar=0){
  const s=this.sessions[sid];if(!s)return[];const ids=s.members.filter(id=>this.devices[id].present),hasIn=new Set(),next={};
  for(const k of Object.keys(this.committedEdges)){const [a,b]=k.split('>');if(ids.includes(a)&&ids.includes(b)){next[a]=b;hasIn.add(b);}}
  const heads=ids.filter(id=>!hasIn.has(id)).sort(),visited=new Set(),result=[];
  for(const head of [...heads,...ids]){if(visited.has(head))continue;let id=head,source=null,counts={};
   while(id&&!visited.has(id)){visited.add(id);const d=this.devices[id];if(!source)counts={};const duplicate=counts[d.role]||0;counts[d.role]=duplicate+1;
    const notes=this.phrase(d,source,bar,duplicate),available=d.motifAvailable&&d.battery>0;
    const outputAvailable=available&&!d.fault&&!d.loading;
    const p={id,role:d.role,source:source?.id||null,events:outputAvailable&&!d.muted?notes:[],motif:available?notes:[],activity:d.committedActivity,phrase:d.phrase,sessionId:sid};result.push(p);source=available&&notes.length?p:null;id=next[id];
   }
  }return result;
 }
 composeAt(at=this.time){return Object.values(this.sessions).flatMap(s=>this.composeSession(s.id,this.phase(s.members[0],at).bar));}
 compose(bar=0){if(!this.sessions)return super.compose(bar);return Object.values(this.sessions).flatMap(s=>this.composeSession(s.id,bar));}
 snapshot(){return clone({devices:this.devices,sessions:this.sessions,contacts:this.contactInput,committedEdges:this.committedEdges,merges:this.merges,arrangements:this.arrangements,pending:this.pending,time:this.time,revision:this.revision,bpm:this.bpm});}
}
root.DuijuSession=DuijuSession;
})(typeof window!=='undefined'?window:globalThis);
