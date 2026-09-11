/* Harmony v5.0.2: rhythmic chords / broken chords / a bounded answer phrase.
 * Direct-left motif only. Limited D-minor vocabulary; never microphone inference.
 * Broken-chord phrase 2 retains v0.5 event fields and harmonic selection exactly.
 */
(function(root){'use strict';
 const response=root.DuijuResponse||(typeof require==='function'?require('./response.js'):null);
 const clamp=(n,a,b)=>Math.max(a,Math.min(b,Number.isFinite(n)?n:a));
 const wrap=n=>(n%16+16)%16,pc=n=>(n%12+12)%12;
 const distance=(a,b)=>Math.min(wrap(a-b),wrap(b-a));
 const CHORDS=Object.freeze([
  {name:'Dm',notes:[50,57,65]}, {name:'F',notes:[53,60,69]},
  {name:'C',notes:[48,55,64]}, {name:'Gm',notes:[55,62,70]},
  {name:'Bb',notes:[46,53,62]}, {name:'Am',notes:[45,52,60]}
 ]);
 function pitchEvidence(q){
  const weights=Array(12).fill(0);if(!q)return weights;
  for(const e of q.events){
   if(!['tone','grain','chord'].includes(e.kind))continue;
   const pitches=Array.isArray(e.harmony)?e.harmony:Array.isArray(e.notes)?e.notes:[e.note];
   const valid=pitches.filter(Number.isFinite);if(!valid.length)continue;
   const weight=(.3+.7*e.vel)*(e.accent?1.15:1)*Math.sqrt(e.span)/valid.length;
   for(const n of valid)weights[pc(n)]+=weight;
  }return weights;
 }
 function choose(q){
  const weights=pitchEvidence(q),total=weights.reduce((s,n)=>s+n,0);
  if(total===0)return CHORDS[0];
  const scores=CHORDS.map((c,i)=>{
   const pcs=c.notes.map(pc);let score=0;
   for(let n=0;n<12;n++)if(weights[n]){
    if(pcs.includes(n))score+=weights[n];
    else if(pcs.some(p=>Math.min(pc(n-p),pc(p-n))===1))score-=weights[n]*.16;
   }return {c,i,score:score/total};
  }).sort((a,b)=>b.score-a.score||a.i-b.i);
  return scores[0].c;
 }
 function voicing(notes,duplicate){
  const ns=notes.slice();if(duplicate%2){ns[0]+=12;ns.sort((a,b)=>a-b);}return ns;
 }
 // This path is deliberately the v0.5 phrase-2 rule, including its duration and gain.
 function brokenPositions(q,activity){
  const targets=activity<.25?[0,6,12]:activity>.80?[0,2,5,7,10,12,14]:[0,3,6,10,13];
  if(!q)return targets;
  const used=new Set();return targets.map((t,i)=>{
   const target=wrap(t+q.first+1);let best=target,score=-Infinity;
   for(const delta of [0,-1,1]){const s=wrap(target+delta);if(used.has(s))continue;
    const v=q.release[s]*.65+q.space[s]*.45+q.onset[wrap(s-1)]*.3-Math.abs(delta)*.16;
    if(v>score){best=s;score=v;}}
   used.add(best);return best;
  }).sort((a,b)=>a-b);
 }
 function broken(q,a,tones,selected,stepTime){
  const slots=brokenPositions(q,a),expression=a-.5;
  const mean=q?q.meanVelocity:.55,span=q?q.meanSpan:1.7;
  const center=q?q.events.reduce((s,n)=>s+n.step*n.vel,0)/q.events.reduce((s,n)=>s+n.vel,0)/15:.45;
  return slots.map((step,i)=>{
   const next=slots[i+1]??16,deadline=Math.max(.08,(next-step)*stepTime-.05);
   const grace=.20+.18*a+(span-1)*.014+center*.025;
   const dur=clamp(grace*(1+expression*.45),.07,deadline);
   const notes=[tones[[0,1,2,1,0,2,1][i%7]]];
   const vel=clamp(.57+(mean-.55)*.10+(center-.45)*.022+(i===0?.035:0),.35,.78);
   return {step,kind:'chord',note:notes[notes.length-1],notes,harmony:tones.slice(),chord:selected.name,
    articulation:'broken',vel,dur,maxDuration:deadline,
    accent:i===0||i===Math.floor(slots.length/2),expression};
  });
 }
 function preceding(q,step){
  return q?.events.reduce((best,e)=>!best||wrap(step-e.step)<wrap(step-best.step)?e:best,null);
 }
 function shortPlan(q,a){
  const cells=a<.25?[1,7,12]:a>.78?[1,4,7,10,13]:[1,5,9,13],steps=[];
  for(const cell of cells){
   const target=wrap(cell+(q?.first||0));let best=null,score=-Infinity;
   for(let delta=-2;delta<=2;delta++){
    const s=wrap(target+delta);if(steps.some(x=>distance(x,s)<2))continue;
    const musical=q?q.release[s]*.85+q.space[s]*.5+q.onset[wrap(s-1)]*.55-q.onset[s]*.26:0;
    const rank=musical-Math.abs(delta)*.16;
    if(rank>score){best=s;score=rank;}
   }
   if(best!==null)steps.push(best);
  }
  return steps.sort((a,b)=>a-b).map(step=>({step,closing:true}));
 }
 function answerPlan(q,a){
  // A local answer has an entrance and a closing chord. At low activity it says
  // one short sentence; normally two, with several empty steps between them.
  const targets=a<.30?[10]:[4,11],starts=[],long=a>.74,maxStart=long?12:13;
  for(const target of targets){
   let best=null,score=-Infinity;
   for(let s=1;s<=maxStart;s++){
    if(starts.some(x=>Math.abs(x-s)<6))continue;
    const space=q?(q.space[s]+q.space[s+1]+q.space[s+2])/3:0;
    const musical=q?q.release[s]*1.10+space*.78+q.onset[wrap(s-1)]*.18-q.onset[s]*.5:0;
    const rank=musical-Math.abs(s-target)*.12;
    if(rank>score){best=s;score=rank;}
   }
   if(best!==null)starts.push(best);
  }
  return starts.sort((a,b)=>a-b).flatMap(start=>long?
   [{step:start,closing:false},{step:start+1,closing:false},{step:start+3,closing:true}]:
   [{step:start,closing:false},{step:start+2,closing:true}]);
 }
 // Compact three-part voicings, with one of each chord pitch class. Register
 // changes change actual MIDI notes, not just array order or metadata.
 const SHORT_VOICINGS=CHORDS.flatMap(chord=>{
  const pcs=new Set(chord.notes.map(pc)),out=[];
  for(let low=45;low<=65;low++)if(pcs.has(pc(low)))
   for(let mid=low+3;mid<=Math.min(low+12,75);mid++)if(pcs.has(pc(mid))&&pc(mid)!==pc(low))
    for(let high=Math.max(60,mid+3);high<=Math.min(low+19,81);high++)
     if(pcs.has(pc(high))&&pc(high)!==pc(low)&&pc(high)!==pc(mid))out.push({chord,notes:[low,mid,high]});
  return out;
 });
 function fitChord(chord,weights){
  const total=weights.reduce((s,n)=>s+n,0);if(!total)return 0;
  const pcs=chord.notes.map(pc);let score=0;
  for(let n=0;n<12;n++)if(weights[n]){
   if(pcs.includes(n))score+=weights[n];
   else if(pcs.some(p=>Math.min(pc(n-p),pc(p-n))===1))score-=weights[n]*.16;
  }
  return score/total;
 }
 function shortVoicings(q,plan,selected,duplicate){
  const register=duplicate%2?3:0,anchor=voicing(selected.notes,duplicate),baseTop=selected.notes[2]+register;
  const contours={3:[0,5,0],4:[0,4,7,0],5:[0,4,7,4,0]},contour=contours[plan.length]||[0,4,0];
  const globalWeights=pitchEvidence(q),out=[];
  for(let i=0;i<plan.length;i++){
   const step=plan[i].step;
   const cue=q?.pitched.reduce((best,e)=>!best||wrap(step-e.step)<wrap(step-best.step)?e:best,null);
   // Follow the sounding/recent pitched phrase near this entry, not a drum's
   // MIDI number. Retain global context so weak local notes are not a new key.
   const local=cue?q.pitched.filter(e=>e===cue||wrap(step-e.step)<e.span):[];
   const localWeights=pitchEvidence(local.length?{events:local}:null);
   const fits=new Map(CHORDS.map(c=>[c.name,cue ? .78*fitChord(c,localWeights)+.22*fitChord(c,globalWeights):c===selected?1:-1]));
   const bestFit=Math.max(...fits.values());
   const folded=cue?cue.note+12*Math.round((baseTop-cue.note)/12):baseTop;
   const target=baseTop*.35+folded*.65+contour[i]*.65;
   const previous=out[i-1],first=out[0];
   const available=SHORT_VOICINGS.filter(c=>fits.get(c.chord.name)>=bestFit-.18&&c.notes[2]>=60+register&&c.notes[2]<=77+register);
   const admissible=c=>!previous||(c.notes[2]!==previous.notes[2]&&Math.abs(c.notes[2]-previous.notes[2])<=7);
   const closing=c=>i!==plan.length-1||!first||Math.abs(c.notes[2]-first.notes[2])<=7;
   let candidates=available.filter(c=>admissible(c)&&closing(c));
   // A rare incompatible local chord must not force a melodic jump. Prefer a
   // smoother compatible contextual chord, still from the same bounded scale.
   if(!candidates.length)candidates=SHORT_VOICINGS.filter(c=>c.notes[2]>=60+register&&c.notes[2]<=77+register&&admissible(c)&&closing(c));
   const reference=previous?.notes||anchor;
   const score=c=>fits.get(c.chord.name)*8-Math.abs(c.notes[2]-target)*.90
    -c.notes.reduce((s,n,j)=>s+Math.abs(n-reference[j]),0)*.11
    -Math.abs(c.notes[0]-anchor[0])*.035+(c.chord.name===previous?.chord.name ? .35 : 0);
   candidates.sort((a,b)=>score(b)-score(a)||a.notes[2]-b.notes[2]||a.notes[0]-b.notes[0]);
   const pick=candidates[0];out.push({chord:pick.chord,notes:pick.notes.slice()});
  }
  return out;
 }
 function compose(d,source,bar=0,duplicate=0,bpm=96){
  const a=clamp(d.committedActivity,0,1),variant=clamp(d.phrase||2,1,3),stepTime=60/clamp(bpm,40,200)/4;
  const q=source?.motif?.length?response.analyze(source.motif,bpm):null;
  const selected=choose(q),tones=voicing(selected.notes,duplicate);
  if(variant===2)return broken(q,a,tones,selected,stepTime);
  const plan=variant===1?shortPlan(q,a):answerPlan(q,a),expression=a-.5;
  const motion=variant===1?shortVoicings(q,plan,selected,duplicate):null;
  const mean=q?.meanVelocity??.55,span=q?.meanSpan??1.7;
  const center=q?q.events.reduce((s,n)=>s+n.step*n.vel,0)/q.events.reduce((s,n)=>s+n.vel,0)/15:.45;
  return plan.map((entry,i)=>{
   const {step,closing}=entry,next=plan[i+1]?.step??16,deadline=Math.max(.045,(next-step)*stepTime-.035);
   const cue=preceding(q,step),cueSpan=cue?.span??span;
   const space=q?.space[step]??.7;
   const base=variant===1?stepTime*(.78+.35*a)+Math.min(cueSpan,4)*.012+space*.014:
    closing?stepTime*(1.04+.34*a)+Math.min(cueSpan,4)*.015+space*.015:
    stepTime*(.55+.25*a)+Math.min(cueSpan,4)*.007;
   const dur=clamp(base+(center-.45)*.018,.035,Math.min(deadline,variant===1?.30:closing?.36:.19));
   const eventTones=motion?motion[i].notes:tones,eventChord=motion?motion[i].chord:selected;
   const notes=closing?eventTones.slice():[eventTones[(i+1)%eventTones.length]];
   const vel=clamp((closing?.56:.47)+(mean-.55)*.10+((cue?.vel??mean)-.55)*.065+(center-.45)*.022,.33,.75);
   return {step,kind:'chord',note:notes[notes.length-1],notes,harmony:eventTones.slice(),chord:eventChord.name,
    articulation:variant===1?'short':'answer',vel,dur,maxDuration:deadline,
    accent:closing,expression};
  });
 }
 const api=Object.freeze({compose,pitchEvidence});root.DuijuHarmony=api;
 if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof window!=='undefined'?window:globalThis);


