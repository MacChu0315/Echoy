// Run the frozen production script in its original OfflineAudioContext.
// Usage: node oracle.mjs /tmp/echoy-audio-oracle
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = path.resolve(process.argv[2]);
fs.mkdirSync(output, {recursive:true});
const playwright = process.env.ECHOY_PLAYWRIGHT_MODULE || '/home/zhuzhengming/.npm/_npx/31e32ef8478fbf80/node_modules/playwright/index.mjs';
const {chromium} = await import(playwright);
const browser = await chromium.launch({executablePath:process.env.ECHOY_CHROME || '/opt/google/chrome/chrome',headless:true,args:['--no-sandbox','--disable-dev-shm-usage']});
const page = await browser.newPage();
await page.goto('file://' + path.join(root, 'reference/v4/echoy-arrange-B-v4.html'));
const specs=[];
for(const role of ['pulse','voice','dust','harmony']) for(let phrase=1;phrase<=3;phrase++)
  specs.push({name:role+'-'+phrase,role,phrase,density:.65,steps:[0,2,10],original:role==='pulse'&&phrase===1});
specs.push(
  {name:'pulse-original-single',role:'pulse',phrase:1,density:0,steps:[0],original:true},
  {name:'pulse-original-late',role:'pulse',phrase:1,density:1,steps:[4,6],original:true},
  {name:'pulse-linked',role:'pulse',phrase:1,density:.65,steps:[0,2,10],original:false},
  {name:'voice-delay',role:'voice',phrase:3,density:1,steps:[3,10]},
  {name:'dust-tail',role:'dust',phrase:3,density:1,steps:[0,2,7,12]},
  {name:'harmony-late',role:'harmony',phrase:2,density:.8,steps:[3,13,15]});
specs.push(
  {name:'pulse-original-mixed',role:'pulse',phrase:1,density:.8,steps:[0,4,9,13],kinds:['kick','rim','hat','kick'],velocities:[.74,.4,.3,.85],original:true},
  {name:'pulse-original-dense',role:'pulse',phrase:1,density:1,steps:Array.from({length:16},(_,i)=>i),original:true});
for(const role of ['voice','dust','harmony'])
  specs.push({name:'linked-'+role,role,phrase:2,density:.85,steps:[0,2,10],upstreamRole:'voice'});
for(const [kind,role,articulation,notes] of [
  ['kick','pulse','',[]],['rim','pulse','',[]],['hat','pulse','',[]],
  ['tone','voice','',[]],['grain','dust','',[]],
  ['chord','harmony','short',[50,57,65]],['chord','harmony','broken',[53]],['chord','harmony','answer',[55,62,70]],
]) specs.push({name:'isolated-'+kind+(articulation?'-'+articulation:''),role,phrase:2,density:.5,steps:[1],event:{step:1,kind,vel:.74,note:67,dur:.42,maxDuration:.8,notes:notes.length?notes:undefined,harmony:[50,57,65],layer:'anchor',articulation:articulation||undefined}});
specs.push({name:'compressor-overlap',role:'pulse',phrase:2,density:.5,steps:[0],overlap:true});
const manifest=[];
try {
  for(const spec of specs) {
    const result = await page.evaluate(async spec=>{
      const seed=spec.steps.map((step,i)=>({step,kind:spec.kinds?.[i]||'kick',vel:spec.velocities?.[i]??.74}));
      const audio=new RoleAudio();
      let plan=RoleAccompaniment.compose(seed,spec.role,spec.phrase,spec.density);
      if(spec.upstreamRole) plan=ArrangeLinked(plan,RoleAccompaniment.compose(seed,spec.upstreamRole,3,.8));
      if(spec.event) plan={...plan,full:[spec.event],foreground:[spec.event]};
      if(spec.overlap) {const notes=Array.from({length:64},()=>({step:0,kind:'kick',vel:.85,dur:.22,layer:'anchor'}));plan={...plan,full:notes,foreground:notes};}
      let arrays,metrics;
      if(spec.original) ({arrays,metrics}=await audio.prepareRole(seed,spec.role,spec.phrase,spec.density));
      else {
        const [main,full,raw]=await Promise.all([audio.renderRoleNotes(plan.foreground,spec.role),audio.renderRoleNotes(plan.full,spec.role),audio.renderNotes(plan.source)]);
        const matched=ReplyAB.normalise({sparse:main,layered:full}),s=ReplyAB.stats(raw),gain=s.rms?Math.min(.65/s.peak,matched.metrics.targetRms/s.rms):0;
        arrays={source:Float32Array.from(raw,x=>x*gain),foreground:matched.sparse,full:matched.layered};metrics=matched.metrics;
      }
      const encode=data=>{const bytes=new Uint8Array(data.buffer,data.byteOffset,data.byteLength);let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(binary);};
      return {plan,metrics,arrays:Object.fromEntries(Object.entries(arrays).map(([kind,array])=>[kind,encode(array)]))};
    },spec);
    for(const [kind,bytes] of Object.entries(result.arrays)) fs.writeFileSync(path.join(output,spec.name+'-'+kind+'.f32'),Buffer.from(bytes,'base64'));
    manifest.push({...spec,plan:result.plan,metrics:result.metrics});
    console.log('oracle',spec.name);
  }
  for(const cue of ['tap','guide','capture']) {
    const result = await page.evaluate(async cue=>{
      const frames=cue==='tap'?8640:720,context=new OfflineAudioContext(1,frames,24000);
      if(cue==='tap') {
        const synth=new DuijuSynth(context);synth.setVolume(.55);
        synth.note('finger',{kind:'kick',vel:.78,dur:.22,note:38,expression:0},0,0);
      } else {
        const oscillator=context.createOscillator(),gain=context.createGain();
        oscillator.type='sine';oscillator.frequency.value=cue==='capture'?1500:1050;
        gain.gain.setValueAtTime(.00001,0);gain.gain.linearRampToValueAtTime(cue==='capture'?.075:.05,.001);gain.gain.exponentialRampToValueAtTime(.00001,.027);
        oscillator.connect(gain);gain.connect(context.destination);oscillator.start(0);oscillator.stop(.03);
      }
      const samples=(await context.startRendering()).getChannelData(0),bytes=new Uint8Array(samples.buffer);let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(binary);
    },cue);
    fs.writeFileSync(path.join(output,'cue-'+cue+'.f32'),Buffer.from(result,'base64'));
  }
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({browser:browser.version(),sampleRate:24000,cases:manifest},null,2));
} finally {await browser.close();}
