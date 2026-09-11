// SPDX-License-Identifier: Apache-2.0
// A bounded PCM output queue only; synthesis stays in echoy_audio on the PC.
class EchoyPCM extends AudioWorkletProcessor {
  constructor(){super();this.queue=[];this.offset=0;this.last=0;this.port.onmessage=({data})=>{
    if(data.clear){this.queue=[];this.offset=0;return;}
    if(data.sequence<=this.last)return;this.last=data.sequence;
    if(this.queue.length>=8){this.queue.shift();this.offset=0;}
    this.queue.push(new Int16Array(data.pcm));
  };}
  process(_inputs,outputs){const out=outputs[0];for(let i=0;i<out[0].length;i++){
    while(this.queue.length&&this.offset>=this.queue[0].length){this.queue.shift();this.offset=0;}
    const p=this.queue[0];out[0][i]=p?p[this.offset]/32768:0;out[1][i]=p?p[this.offset+1]/32768:0;
    if(p)this.offset+=2;
  }return true;}
}
registerProcessor('echoy-pcm',EchoyPCM);
