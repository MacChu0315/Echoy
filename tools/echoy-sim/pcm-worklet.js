// SPDX-License-Identifier: Apache-2.0
// Bounded PCM playback only; synthesis stays in echoy_audio on the PC.
class EchoyPCM extends AudioWorkletProcessor {
  constructor(){super();this.queue=[];this.offset=0;this.samples=0;this.buffering=true;this.last=0;this.generation=0;this.port.onmessage=({data})=>{
    if(data.clear){this.clear();return;}
    if(!Number.isSafeInteger(data.generation)||data.generation<1||!Number.isSafeInteger(data.sequence)||data.sequence<1)return;
    if(data.generation<this.generation)return;
    if(data.generation>this.generation){this.generation=data.generation;this.clear();this.last=0;}
    if(data.sequence<=this.last)return;this.last=data.sequence;
    let pcm=new Int16Array(data.pcm);if(!pcm.length||pcm.length%2)return;
    // 750 ms maximum, including oversized input. Keep newest complete frames.
    const capacity=72000;
    if(pcm.length>capacity)pcm=pcm.slice(-capacity);
    while(this.samples+pcm.length>capacity&&this.queue.length){this.samples-=this.queue.shift().length-this.offset;this.offset=0;}
    this.queue.push(pcm);this.samples+=pcm.length;
  };}
  clear(){this.queue=[];this.offset=0;this.samples=0;this.buffering=true;}
  process(_inputs,outputs){const out=outputs[0];
    // Prefill 192 ms to absorb the measured 126 ms HTTP cadence. Refill only
    // after a true underrun; empty snapshot publications cannot dilute it.
    if(this.buffering&&this.samples>=18432)this.buffering=false;
    for(let i=0;i<out[0].length;i++){
      while(this.queue.length&&this.offset>=this.queue[0].length){this.queue.shift();this.offset=0;}
      const p=this.buffering?null:this.queue[0];
      out[0][i]=p?p[this.offset]/32768:0;out[1][i]=p?p[this.offset+1]/32768:0;
      if(p){this.offset+=2;this.samples-=2;}else if(!this.queue.length)this.buffering=true;
    }return true;
  }
}
registerProcessor('echoy-pcm',EchoyPCM);
