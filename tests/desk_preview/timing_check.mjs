// Exercise the actual browser controller with a deterministic clock and transport.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=JSON.parse(readFileSync(0,'utf8')).script;
function fixture(){
  let now=0,next=0;const timers=new Map(),sent=[];
  const state={ready:true,connected:true,audio_connected:true,export_available:false,
    tiles:{A:{present:true,role:'pulse'},B:{present:false,role:'voice'},C:{present:false,role:'harmony'}},edges:[]};
  const elements=new Map();
  function element(key){if(!elements.has(key))elements.set(key,{value:'A',textContent:'',style:{},dataset:{},hidden:false,
    classList:{add(){},remove(){}},addEventListener(){},querySelector:s=>element(key+s),click(){}});return elements.get(key);}
  const context=vm.createContext({console,Date:{now:()=>now},
    document:{getElementById:element,querySelector:element,querySelectorAll:()=>[],addEventListener(){},createElement:element},
    window:{addEventListener(){}},
    setTimeout:(fn,ms)=>{const id=++next;timers.set(id,{fn,at:now+ms});return id;},clearTimeout:id=>timers.delete(id),
    fetch:async(path,options)=>{
      if(path==='/command'){
        const body=JSON.parse(options.body);sent.push({at:now,...body});
        if(body.action==='present')state.tiles[body.tile].present=body.present;
        if(body.action==='condition')state.tiles[body.tile].condition=body.condition;
        if(body.action==='edges')state.edges=body.edges;
      }
      return {ok:true,json:async()=>structuredClone(state)};
    }});
  vm.runInContext(source,context);
  const flush=async()=>{for(let i=0;i<25;i++)await Promise.resolve();};
  async function advance(to){await flush();while(true){const due=[...timers].filter(([,v])=>v.at<=to).sort((a,b)=>a[1].at-b[1].at)[0];if(!due)break;now=due[1].at;timers.delete(due[0]);due[1].fn();await flush();}now=to;await flush();}
  return {sent,advance,call:code=>vm.runInContext(code,context)};
}
{
  const f=fixture();await f.call('present("B",true,{x:548,y:22})');
  assert(f.sent.some(c=>c.action==='condition'&&c.condition==='loading'),'Restoring a tile must expose the loading state');
  await f.advance(219);assert(!f.sent.some(c=>c.condition==='normal'));
  await f.advance(220);assert(f.sent.some(c=>c.condition==='normal'&&c.at===220));
  await f.advance(669);assert(!f.sent.some(c=>c.action==='edges'),'Edges must wait 450ms after boot');
  await f.advance(670);const edge=f.sent.find(c=>c.action==='edges');
  assert.deepEqual(edge.edges,[{left:'A',right:'B'}]);
  assert(!f.sent.some(c=>c.action==='play'||c.action==='example'),'Restore never enables output');
}
{
  const f=fixture();await f.call('present("B",true,{x:548,y:22})');await f.advance(100);
  await f.call('present("B",false)');await f.advance(1000);
  assert(!f.sent.some(c=>c.action==='condition'&&c.condition==='normal'),'A removed tile must not be recovered by its old boot timer');
  assert(f.sent.filter(c=>c.action==='edges').every(c=>c.edges.length===0));
}
{
  const f=fixture();await f.call('present("B",true,{x:548,y:22})');await f.advance(100);
  await f.call('present("B",false)');await f.advance(150);
  await f.call('present("B",true,{x:548,y:22})');await f.advance(369);
  assert(!f.sent.some(c=>c.condition==='normal'),'The first boot must not finish the second boot');
  await f.advance(370);assert.equal(f.sent.filter(c=>c.condition==='normal').length,1);
  await f.advance(819);assert(!f.sent.some(c=>c.action==='edges'));
  await f.advance(820);assert.equal(f.sent.filter(c=>c.action==='edges').length,1);
}
console.log('PASS 450ms contact confirmation, 220ms boot, removed/replaced boot cancellation');
