import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,openSync,closeSync,mkdtempSync,rmSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import vm from 'node:vm';
const html=readFileSync('reference/opening/echoy-opening-ab.html','utf8');
const hash=readFileSync('reference/opening/SHA256SUMS','utf8').split(' ')[0];
assert.equal(createHash('sha256').update(html).digest('hex'),hash);
const ctx=vm.createContext({});
for(const name of ['PulseLab','ReplyAB','RoleAccompaniment','OpeningStudy']) {
 const marker=name==='OpeningStudy'?'root.OpeningStudy=api':`root.${name}=api`;
 const at=html.indexOf(marker),start=html.lastIndexOf('(function(root)',at);
 const end="})(typeof window!=='undefined'?window:globalThis);";
 assert(at>=0&&start>=0);vm.runInContext(html.slice(start,html.indexOf(end,at)+end.length),ctx);
}
const dir=mkdtempSync(tmpdir()+'/echoy-pickup-oracle-');
try {
 const compile=spawnSync('cc',['-std=c11','-O2','-Wall','-Wextra','-Werror','-Iprojects/echoy/main',
 'tests/pickup/oracle_runner.c',...['music','arrange','pickup'].map(n=>`projects/echoy/main/echoy_${n}.c`),'-lm','-o',dir+'/oracle'],{encoding:'utf8'});
 assert.equal(compile.status,0,compile.stderr);
 const cases=[];
 for(let role=0;role<4;role++)for(let phrase=1;phrase<=3;phrase++)
 for(const density of [0,.1,.49,.5,.51,.8,1])for(const mask of [0,1,5,2,6,16,80,256,0x5000,0x8000,0xfffe])
 for(let depth=0;depth<3;depth++) cases.push([role,phrase,density,mask,depth]);
 writeFileSync(dir+'/input',cases.map(c=>c.join(' ')).join('\n')+'\n');
 const input=openSync(dir+'/input','r'),output=openSync(dir+'/output','w');
 const result=spawnSync(dir+'/oracle',[],{stdio:[input,output,'pipe'],encoding:'utf8',timeout:30000});
 closeSync(input);closeSync(output);
 assert.equal(result.status,0,result.stderr);
 const lines=readFileSync(dir+'/output','utf8').trim().split('\n'); assert.equal(lines.length,cases.length*3);
 const roles=['pulse','voice','dust','harmony'],kinds=['kick','rim','hat','tone','grain','chord'];
 function toJS(n) { return {step:n.step,vel:n.velocity,dur:n.duration,kind:kinds[n.kind],note:n.note,
 ...(n.notes.length?{notes:n.notes}:{}),...(n.harmony.length?{harmony:n.harmony}:{}),layer:n.layer===0?'anchor':'detail'}; }
 function fromJS(n) { return {step:n.step,velocity:n.vel,duration:n.dur,max_duration:n.maxDuration??0,
 source_step:n.sourceStep,expression:n.expression,kind:kinds.indexOf(n.kind),layer:4,
 articulation:n.articulation==='answer'?3:0,note:n.note,notes:Array.from(n.notes??[]),harmony:Array.from(n.harmony??[])}; }
 for(let i=0;i<cases.length;i++) {
  const p=JSON.parse(lines[i*3]),actual=JSON.parse(lines[i*3+1]);
  const original={role:roles[p.role],density:p.density,rootSeed:p.source,full:p.events.map(toJS),foreground:p.events.filter(n=>n.layer===0).map(toJS)};
  const untouched=JSON.stringify(original), decorated=ctx.OpeningStudy.apply(original,'pickup');
  assert.equal(JSON.stringify(original),untouched);
  assert.deepEqual(actual.events,Array.from(decorated.openingIntro??[],fromJS),JSON.stringify(cases[i]));
  assert.equal(ctx.OpeningStudy.base(decorated),original);
  assert.equal(JSON.stringify(ctx.OpeningStudy.apply(decorated,'pickup').openingIntro),JSON.stringify(decorated.openingIntro));
 }
 const mixCompile=spawnSync('cc',['-std=c11','-O2','-Wall','-Wextra','-Werror','-Iprojects/echoy/main',
 'tests/pickup/mix_runner.c',...['music','audio','pickup'].map(n=>`projects/echoy/main/echoy_${n}.c`),'-lm','-o',dir+'/mix'],{encoding:'utf8'});
 assert.equal(mixCompile.status,0,mixCompile.stderr);
 const mixed=spawnSync(dir+'/mix',[dir+'/pcm'],{encoding:'utf8',timeout:30000});
 assert.equal(mixed.status,0,mixed.stderr);
 const bytes=readFileSync(dir+'/pcm');const samples=new Float32Array(bytes.buffer,bytes.byteOffset,bytes.length/4);
 let error=0;
 for(const [index,line] of mixed.stdout.trim().split('\n').entries()) {
  const p=JSON.parse(line),at=index*180000;
  const base=samples.slice(at,at+60000),raw=samples.slice(at+60000,at+120000),actual=samples.slice(at+120000,at+180000);
  const data={sampleRate:24000,arrays:{full:base},metrics:{gains:{layered:p.gain}}};
  const plan={openingBase:{rootSeed:[{step:p.head}]},openingIntro:[{step:p.start}]};
  const expected=ctx.OpeningStudy.mix(data,plan,raw);
  assert.equal(expected.boundary,p.boundary);
  assert.equal(p.end,expected.boundary-288);
  for(let i=0;i<60000;i++) {
   error=Math.max(error,Math.abs(actual[i]-expected.full[i]));
   assert(Math.abs(actual[i]-expected.full[i])<=6e-8);
   if(i>=p.end)assert.equal(actual[i],base[i]);
  }
 }
 console.log(`PASS 24 real C synth/office-profile PCM layers vs frozen OpeningStudy.mix; max float error ${error}, exact tail`);
 console.log(`PASS ${cases.length} actual shared-C plans (4 roles, 3 expressions, 7 densities, 11 inputs, 3 chain depths) vs frozen OpeningStudy.apply`);
} finally {rmSync(dir,{recursive:true,force:true});}
