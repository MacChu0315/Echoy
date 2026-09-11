#!/usr/bin/env node
// Development-only oracle fixture generator. Native/firmware execution uses C.
import fs from 'node:fs';
import vm from 'node:vm';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
const root = new URL('../', import.meta.url);
const source = fs.readFileSync(new URL('reference/Echoy_v5.0.3.html', root), 'utf8');
assert.equal(crypto.createHash('sha256').update(source).digest('hex'), 'd23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5');
const context = vm.createContext({});
vm.runInContext(source.slice(source.indexOf('/* Complete-phrase response'), source.indexOf('/* Simulator only:')), context);
const engine = new context.DuijuEngine();
const fields = ['step','kind','vel','note','dur','accent','expression','notes','harmony','chord','articulation','maxDuration'];
const encode = events => events.map(e => fields.map(f => e[f] ?? null));
const defaults = {pulse:.58,voice:.56,dust:.48,harmony:.5};
const compose = input => {
  engine.bpm = input.bpm;
  return engine.phrase({role:input.role,phrase:input.phrase,committedActivity:input.activity}, input.motif === null ? null : {motif:input.motif}, input.bar, input.duplicate);
};
const chainRoles = [['pulse','voice','harmony'],['harmony','dust','pulse'],['harmony','voice','pulse'],['pulse','dust','harmony'],...Object.keys(defaults).map(r=>[r,r,r])];
const chains=[];
for (const roles of chainRoles) for(const phrase of [1,2,3]) for(const bar of [0,2]) {
  const counts={};let motif=null;
  const steps=roles.map(role=>{
    const input={role,phrase,activity:defaults[role],bar,duplicate:counts[role]||0,bpm:96};counts[role]=(counts[role]||0)+1;
    motif=compose({...input,motif});return {input,events:encode(motif)};
  });
  chains.push({roles,phrase,bar,steps});
}
const motifs = [
  [{step:15.5,kind:'tone',vel:.05,note:-13,dur:.001,accent:false}],
  [{step:.5,kind:'tone',vel:1.4,note:-1,dur:9,accent:true},{step:.49,kind:'grain',vel:-.2,note:133,dur:.01,accent:false}],
  [{step:0,kind:'chord',vel:.8,note:65,notes:[50,57,65],harmony:[],dur:8,accent:false}],
  [{step:4,kind:'tone',vel:.4,note:68,dur:.42,accent:false},{step:4,kind:'tone',vel:.6,note:62,dur:.11,accent:true}],
  [{step:-1,kind:'tone',vel:.5,note:62,dur:.1},{step:16,kind:'chord',vel:.5,note:65,dur:.3},{step:8,kind:'grain',vel:.5,note:77,dur:0}],
  Array.from({length:16},(_,step)=>({step,kind:'chord',vel:.05+(step%4)*.3,note:65,notes:[50,57,65],harmony:[50,57,65],dur:.01+step*.2,accent:step%3===0})),
];
const cases=[];
for(const role of Object.keys(defaults)) for(const phrase of [1,2,3]) for(const bpm of [20,40,96,200,240]) for(const motif of motifs) {
  const input={role,phrase,activity:defaults[role],bar:2,duplicate:1,bpm,motif};
  cases.push({input,events:encode(compose(input))});
}
const path = new URL('tests/cases/music-edges.json', root);
const json=JSON.stringify({schemaVersion:1,eventFields:fields,chains,cases})+'\n';
if(process.argv.includes('--check')){assert.equal(fs.readFileSync(path,'utf8'),json);console.log(`PASS ${chains.length} chain fixtures / ${cases.length} boundary fixtures`);}
else fs.writeFileSync(path,json);
