/* Complete-phrase response rules, v0.4. Pure, deterministic, bounded to one 16-step bar.
 * Input is ONLY the direct left neighbour's complete motif, never the chain head,
 * physical coordinates, right-hand members, output level or device identity.
 * Event budgets express a role's own syntax, not the number of upstream accents.
 */
(function(root){'use strict';
const clamp=(v,lo,hi)=>Math.min(hi,Math.max(lo,Number.isFinite(v)?v:lo));
const wrap=n=>(n%16+16)%16;
const distance=(a,b)=>Math.min(wrap(a-b),wrap(b-a));
const initials={pulse:.58,voice:.56,dust:.48};
const palette=[62,64,65,67,69,70,72,74]; // D minor, same musical setting as the baseline.
const shapes={
 pulse:[{slots:[0,4,8,12],delay:2},{slots:[0,3,7,10,14],delay:1},{slots:[0,6,11],delay:3}],
 voice:[{slots:[0,4,8,12],delay:2},{slots:[1,4,6,9,13],delay:3},{slots:[0,6,12],delay:4}],
 dust:[{slots:[1,6,11,15],delay:1},{slots:[2,9,14],delay:2},{slots:[0,5,10],delay:4}]
};

function analyze(motif,bpm=96){
 const stepSeconds=60/bpm/4;
 const events=motif.filter(n=>Number.isFinite(n.step)&&n.step>=0&&n.step<16&&Number.isFinite(n.dur)&&n.dur>0)
  .map(n=>({...n,step:Math.round(n.step),vel:clamp(n.vel,.05,1),span:clamp(n.dur/stepSeconds,.25,n.kind==='chord'?16:6)}))
  .sort((a,b)=>a.step-b.step||String(a.kind).localeCompare(String(b.kind)));
 if(!events.length)return null;
 const onset=Array(16).fill(0),release=Array(16).fill(0),held=Array(16).fill(0);
 for(const n of events){
  // A weak note remains an input. Accent is a modest weight, never an eligibility gate.
  const strength=(.30+.70*n.vel)*(n.accent?1.12:1);
  onset[n.step]+=strength;
  const end=wrap(n.step+n.span);
  for(let s=0;s<16;s++){
   release[s]+=strength*Math.max(0,1-distance(s,end)/1.6);
   const elapsed=wrap(s-n.step);
   if(elapsed<n.span)held[s]+=strength*(1-.25*elapsed/n.span);
  }
 }
 // Fixed saturation, not per-motif peak normalization: weak-event dynamics remain useful.
 const saturate=arr=>arr.map(x=>1-Math.exp(-x));
 const on=saturate(onset),off=saturate(release),occupied=saturate(held);
 return{events,onset:on,release:off,held:occupied,space:occupied.map(x=>1-x),
  first:events[0].step,meanVelocity:events.reduce((v,n)=>v+n.vel,0)/events.length,
  meanSpan:events.reduce((v,n)=>v+n.span,0)/events.length,
  pitched:events.filter(n=>(n.kind==='tone'||n.kind==='grain'||n.kind==='chord')&&Number.isFinite(n.note))};
}

function resizeSlots(slots,count){
 if(count<=slots.length)return Array.from({length:count},(_,i)=>slots[Math.floor(i*slots.length/count)]);
 const out=slots.slice();
 while(out.length<count){out.sort((a,b)=>a-b);let at=0,gap=-1;
  for(let i=0;i<out.length;i++){const span=wrap(out[(i+1)%out.length]-out[i])||16;if(span>gap){gap=span;at=i;}}
  out.push(wrap(out[at]+Math.max(1,Math.floor(gap/2))));
 }
 return out.sort((a,b)=>a-b);
}

function pickSteps(q,shape,role,variant,count){
 const targets=resizeSlots(shape.slots,count).map(s=>wrap(s+q.first+shape.delay));
 const used=new Set(),steps=[];
 const profile=s=>{
  const answer=q.onset[wrap(s-shape.delay)];
  if(role==='pulse')return answer*.95+q.release[s]*.55+q.space[s]*.20;
  if(role==='voice')return answer*.70+q.release[s]*.65+q.space[s]*(variant===3?.72:.42);
  return answer*.45+q.release[s]*.90+(variant===3?q.held[s]*.30:q.space[s]*.45);
 };
 for(const target of targets){
  // Local phrase cells keep a beginning/middle/ending while following upstream detail.
  let best=null,bestScore=-Infinity;
  for(let shift=-2;shift<=2;shift++){
   const s=wrap(target+shift);if(used.has(s))continue;
   const crowd=steps.reduce((v,n)=>v+(distance(s,n)<2?.40:0),0);
   const score=profile(s)-Math.abs(shift)*.19-crowd+(shift===0?.025:0);
   if(score>bestScore){bestScore=score;best=s;}
  }
  if(best===null){for(let i=0;i<16;i++)if(!used.has(wrap(target+i))){best=wrap(target+i);break;}}
  used.add(best);steps.push(best);
 }
 return steps.sort((a,b)=>a-b);
}

function preceding(events,step){
 return events.reduce((best,n)=>!best||wrap(step-n.step)<wrap(step-best.step)?n:best,null);
}
function pitch(q,step,delay,index,variant,duplicate,role,bar){
 const cue=preceding(q.pitched,wrap(step-delay));
 let degree;
 if(cue?.harmony?.length){
  // New chord metadata is only consumed when present; v0.4 inputs are unchanged.
  const tones=[...new Set(cue.harmony.filter(Number.isFinite).map(n=>62+((n-62)%12+12)%12))].sort((a,b)=>a-b);
  if(tones.length)return tones[(index+(variant===2?1:variant===3?2:0))%tones.length]+(role==='dust'?12:duplicate%2?12:0);
 }
 if(cue){
  const folded=62+((cue.note-62)%12+12)%12;
  degree=palette.reduce((best,n,i)=>Math.abs(n-folded)<Math.abs(palette[best]-folded)?i:best,0);
  // Preserve contour; variants select an in-scale response interval, not a raw MIDI drum pitch.
  degree=(degree+(variant===2?2:variant===3?0:1)+(index%3===2?1:0))%palette.length;
 }else degree=(index*(variant===2?2:1)+(variant===3?4:0)+(bar%4>=2?2:0))%palette.length;
 return palette[degree]+(role==='dust'?12:duplicate%2?12:0);
}

function compose(d,source,bar=0,duplicate=0,bpm=96){
 const role=d.role,v=d.phrase||1,a=clamp(d.committedActivity,0,1),q=analyze(source.motif,bpm);
 if(!q)return[];
 const shape=shapes[role][v-1],expression=a-initials[role];
 const count=clamp(shape.slots.length+Math.round(expression*4),1,7);
 const steps=pickSteps(q,shape,role,v,count);
 const events=steps.map((step,i)=>{
  const cue=preceding(q.events,wrap(step-shape.delay));
  const next=steps[(i+1)%steps.length],gap=wrap(next-step)||16;
  const accent=i===0||i===Math.floor(steps.length/2);
  // Bounded additive expression: do not multiply by upstream gain at every link.
  const dynamics=(cue.vel-.55)*.10+(q.meanVelocity-.55)*.08+(q.meanSpan-1.2)*.012;
  if(role==='pulse'){
   const kick=i===0||(v!==2&&i===Math.floor(steps.length/2));
   return{step,kind:kick?'kick':'rim',vel:clamp((kick?.78:.60)+dynamics,.30,.95),note:kick?38:50,dur:kick?.22:.09,accent};
  }
  const note=pitch(q,step,shape.delay,i,v,duplicate,role,bar);
  if(role==='voice'){
   const base=v===2?.16+a*.13:v===3?.45+a*.34:.21+a*.14;
   const dur=clamp(base+(cue.span-1)*.022+q.space[step]*.035,.12,Math.min(1.05,gap*60/bpm/4+.20));
   return{step,kind:'tone',vel:clamp(.54+(accent?.10:0)+a*.06+dynamics,.25,.88),note,dur,accent};
  }
  const base=v===2?.09+a*.08:v===3?.32+a*.30:.14+a*.20;
  const dur=clamp(base+(cue.span-1)*.018+q.release[step]*.025,.08,Math.min(.90,gap*60/bpm/4+.12));
  return{step,kind:'grain',vel:clamp((v===2?.38:v===3?.42:.32+a*.28)+dynamics,.18,.70),note,dur,accent};
 });
 if(role==='pulse'&&a>.52){
  const hats=v===3?(a>.8?1:0):(a>.82?3:1);
  const free=Array.from({length:16},(_,s)=>s).filter(s=>!events.some(e=>e.step===s))
   .sort((s,t)=>(q.space[t]+q.release[t]*.35+(t%2?.12:0))-(q.space[s]+q.release[s]*.35+(s%2?.12:0))||s-t);
  for(const step of free.slice(0,hats))events.push({step,kind:'hat',vel:.20+a*.14,note:84,dur:.05,accent:false});
 }
 return events.sort((x,y)=>x.step-y.step).map(e=>({...e,expression,dur:e.dur*(1+expression*.45)}));
}
const api=Object.freeze({analyze,compose});root.DuijuResponse=api;
if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof window!=='undefined'?window:globalThis);


