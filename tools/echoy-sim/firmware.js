// SPDX-License-Identifier: Apache-2.0
// Desktop adapter only. The engine object below is an observation of C snapshots.
// It never composes music, elects peers, commits state or draws a product screen.
(function(root){'use strict';
const $=id=>document.getElementById(id),I=root.EchoyI18n,R=root.DUIJU_ROLES,ids=['A','B','C'];
const tr=(zh,en)=>I.locale==='en'?en:zh;
root.duijuEscape=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
class PCMOutput {
 constructor(){this.playing=false;this.silent=true;this.volume=.55;this.nodes=[];}
 async start(silent=false){this.silent=silent;this.playing=true;if(silent)return;
  if(!this.ctx){this.ctx=new AudioContext({sampleRate:48000});await this.ctx.audioWorklet.addModule('/pcm-worklet.js');this.gain=this.ctx.createGain();this.gain.gain.value=this.volume;this.gain.connect(this.ctx.destination);
   for(let i=0;i<3;i++){const node=new AudioWorkletNode(this.ctx,'echoy-pcm',{outputChannelCount:[2]});node.connect(this.gain);this.nodes.push(node);}}
  await this.ctx.resume();
 }
 feed(answer){if(!this.ctx||this.nodes.length!==3||this.silent)return;answer.devices.forEach((d,i)=>{const bytes=Uint8Array.from(atob(d.pcm),c=>c.charCodeAt(0));this.nodes[i].port.postMessage({generation:answer.generation,sequence:answer.sequence,pcm:bytes.buffer},[bytes.buffer]);});}
 stop(){this.nodes.forEach(n=>n.port.postMessage({clear:true}));this.ctx?.suspend();this.playing=false;}
}
class Frames {
 constructor(app){this.app=app;this.units={};for(const id of ids){const el=document.createElement('article');el.className='unit';el.dataset.id=id;
  el.innerHTML=`<div class="device-handle" tabindex="0" role="button" aria-label="拖动设备 ${id} 外壳" title="拖动外壳；方向键移动，Shift 加速"><span class="device-id-badge">${id}</span><span class="handle-grip">⠿</span><span class="handle-copy">拖动外壳</span><span class="caption-state"></span></div><div class="unit-shell"><div class="screen" data-device="${id}" aria-label="设备 ${id} LVGL 产品屏幕"><img width="480" height="480" alt="${id} LVGL framebuffer" draggable="false" style="display:block;width:480px;height:480px;touch-action:none"></div><div class="edge-light edge-left"></div><div class="edge-light edge-right"></div></div><div class="unit-caption"><span class="clock-caption">C · LVGL · DSP</span><span class="beat-dots"><i></i><i></i><i></i><i></i></span></div>`;
  const screen=el.querySelector('.screen'),img=screen.querySelector('img');screen.style.touchAction='none';screen.tabIndex=0;
  let pointer=null;const touch=(phase,ev)=>{const r=screen.getBoundingClientRect();return {op:'touch',id,phase,x:Math.round((ev.clientX-r.left)*480/r.width),y:Math.round((ev.clientY-r.top)*480/r.height)};};
  screen.addEventListener('pointerdown',ev=>{if(ev.button!==0||pointer!==null)return;ev.preventDefault();pointer=ev.pointerId;screen.setPointerCapture(pointer);app.audio.start(false).catch(e=>app.toast(e.message));app.send(touch(0,ev));});
  screen.addEventListener('pointermove',ev=>{if(pointer===ev.pointerId)app.send(touch(1,ev));});
  for(const [event,phase] of [['pointerup',2],['pointercancel',3]])screen.addEventListener(event,ev=>{if(pointer!==ev.pointerId)return;app.send(touch(phase,ev));pointer=null;});
  screen.addEventListener('keydown',ev=>{if(ev.key===' '){ev.preventDefault();app.click(id,330,425);}else if(ev.key==='Escape')app.click(id,60,55);});
  this.units[id]={el,screen,img};$('stage').appendChild(el);I.apply(el);
 }}
 render(){for(const id of ids){const {el,screen}=this.units[id],d=this.app.engine.devices[id],p=this.app.positions[id];el.hidden=!d.present;el.style.setProperty('--accent',R[d.role].color);
  if(p&&this.app.movingUnit!==id){el.style.left=p.x+'px';el.style.top=p.y+'px';}
  el.querySelector('.caption-state').textContent=d.transport;screen.setAttribute('aria-label',`${id} LVGL · ${R[d.role].name} · ${d.transport}`);
  el.querySelector('.clock-caption').textContent=`C / LVGL · r${d.revision||0}`;
 }}
 async frames(answer,current=()=>true){await Promise.all(answer.devices.map(async d=>{const u=this.units[d.id];const image=new Image();image.src=d.frame;await image.decode();if(!current()||this.app.lastGeneration!==answer.generation||this.app.lastSequence!==answer.sequence)return;u.img.src=d.frame;u.screen.dataset.generation=answer.generation;u.screen.dataset.revision=d.revision;u.screen.dataset.audioRevision=d.audio_revision;}));}
 drawAll(){}
}
class App {
 constructor(){this.positions={A:{x:72,y:38},B:{x:646,y:56}};this.spatial=new root.DuijuSpatial();this.clockFaults={};this.movingUnit=null;this.lastSequence=0;this.lastGeneration=0;this.connectionGeneration=1;this.pending=Promise.resolve();this.queueSize=0;this.busy=false;this.connected=true;this.diagnostics={};
  const initial=(id,i)=>({id,present:i<2,role:['pulse','voice','dust'][i],phrase:1,activity:[.58,.56,.48][i],transport:'paused',linkOnline:true,battery:86,loading:false,wifi:true,discovered:[]});
  this.engine={devices:Object.fromEntries(ids.map((id,i)=>[id,initial(id,i)])),committedEdges:{},contactInput:{},pending:{phrases:{}},
   beginMove:id=>this.send({op:'fault',id,name:'moving',value:1}),endMove:id=>this.send({op:'fault',id,name:'moving',value:0}),syncFor:id=>this.engine.devices[id].syncing?{stage:'preparing'}:null};
  this.audio=new PCMOutput();this.view=new Frames(this);this.desk=new root.DuijuDesk(this);this.bind();this.mapping();this.render();
  const banner=document.createElement('p');banner.id='firmware-mode';banner.className='preview-warning';banner.textContent='固件行为模式 · 本地开发注入 · 3 × C core/control · LVGL 屏幕帧 · C DSP 声音。桌面接触为模拟，不能证明真实磁吸或声学同步。';document.querySelector('main').prepend(banner);
  const reconnect=document.createElement('button');reconnect.id='bridge-reconnect';reconnect.hidden=true;reconnect.addEventListener('click',()=>this.reconnect());banner.after(reconnect);
 }
 get physicalOrder(){return ids.filter(id=>this.engine.devices[id].present&&this.positions[id]);}
 target(){return $('target').value;}
 async start(){const guide=root.duijuGuide,targets=guide.targets.bind(guide);guide.targets=()=>{const found=targets();return found.length?found:this.physicalOrder.map(id=>this.view.units[id].screen);};I.apply(document.body);this.locale();await this.send({op:'locale',english:I.locale==='en'});await this.send({op:'snapshot'});this.timer=setInterval(()=>{if(!this.busy&&!document.hidden&&this.connected)this.send({op:'tick',ms:40});},40);}
 disconnect(message){this.connected=false;this.connectionGeneration++;this.inflight?.abort();this.inflight=null;this.audio.stop();$('bridge-reconnect').hidden=false;if(message)this.toast('桥连接中断：'+message);}
 reconnect(){this.disconnect();this.connected=true;$('bridge-reconnect').hidden=true;return this.send({op:'snapshot'});}
 send(request){if(!this.connected)return Promise.resolve();if(this.queueSize>=64){this.toast('输入队列已满，请稍后重试。');return Promise.resolve();}this.queueSize++;
  const generation=this.connectionGeneration,current=()=>this.connected&&this.connectionGeneration===generation;
  const task=async()=>{let controller;try{if(!current())return;this.busy=true;controller=new AbortController();this.inflight=controller;
    const response=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(request),signal:controller.signal});
    if(!current())return;if(!response.ok)throw new Error((await response.json()).error||`HTTP ${response.status}`);
    const answer=await response.json();if(!current())return;
    if(!Number.isSafeInteger(answer.generation)||answer.generation<1||!Number.isSafeInteger(answer.sequence)||answer.sequence<1)throw new Error('Invalid publication identity');
    if(answer.generation<this.lastGeneration||(answer.generation===this.lastGeneration&&answer.sequence<=this.lastSequence))return;
    this.lastGeneration=answer.generation;this.lastSequence=answer.sequence;this.update(answer);await this.view.frames(answer,current);if(!current())return;this.audio.feed(answer);return answer;
   }catch(e){if(current())this.disconnect(e.message);}finally{this.queueSize--;if(this.inflight===controller)this.inflight=null;this.busy=false;}};
  this.pending=this.pending.then(task,task);return this.pending;
 }
 update(answer){this.answer=answer;for(const d of answer.devices){const prior=this.engine.devices[d.id];this.engine.devices[d.id]={...prior,...d,error:d.roleError,fault:d.dataFault?'data':d.outputFault?'audio':null,bootId:d.boot,rolePreparing:d.rolePreparing<0?null:d.rolePreparing};if(!d.present&&!this.movingUnit)delete this.positions[d.id];}
  this.engine.committedEdges={};for(const d of answer.devices)for(const [a,b]of d.edges){const left=ids[a-100],right=ids[b-100];if(left&&right)this.engine.committedEdges[left+'>'+right]=true;}
  this.engine.contactInput=Object.fromEntries(answer.contacts.map(([left,right])=>[left+'>'+right,{left,right,state:answer.uncertain.includes(left)||answer.uncertain.includes(right)?'uncertain':'connected'}]));
  this.engine.pending.phrases=Object.fromEntries(answer.devices.filter(d=>d.pendingPhrase).map(d=>[d.id,{value:d.pendingPhrase}]));
  $('event-log').style.whiteSpace='pre-wrap';$('event-log').textContent=answer.log.slice(-14).reverse().join('\n');this.render();
 }
 async click(id,x,y){await this.send({op:'touch',id,phase:0,x,y});await this.send({op:'touch',id,phase:2,x,y});}
 publishContacts(){const edges=this.spatial.edges(this.positions).filter(e=>this.engine.devices[e.left].present&&this.engine.devices[e.right].present).map(e=>[e.left,e.right]);return this.send({op:'contacts',edges,uncertain:ids.filter(id=>this.clockFaults[id])});}
 async bootAt(id,p){if(this.engine.devices[id].battery===0){this.toast('电量已耗尽。请先在高级面板补电，再拖上桌。');return false;}this.positions[id]=p;this.engine.devices[id].present=true;await this.send({op:'power',id,on:true});this.publishContacts();}
 stow(id){delete this.positions[id];this.engine.devices[id].present=false;this.send({op:'power',id,on:false});this.publishContacts();}
 async scenario(name){const id=this.target();if(name==='all-missing'||name==='no-peer'){for(const other of [...this.physicalOrder])if(name==='all-missing'||other!==id)this.stow(other);return;}await this.send({op:'fault',id,name,value:1});}
 async restore(){const id=this.target();this.clockFaults[id]=false;await this.send({op:'fault',id,name:'restore',value:1});await this.publishContacts();this.syncPanel();}
 bind(){
  $('target').addEventListener('change',()=>this.syncPanel());$('battery').addEventListener('input',ev=>{this.send({op:'fault',id:this.target(),name:'battery',value:+ev.target.value});$('battery-out').textContent=ev.target.value+'%';});
  $('network').addEventListener('change',async ev=>{const value=ev.target.value,id=this.target();await this.send({op:'fault',id,name:'wifi-off',value:+(value==='wifi-off')});this.send({op:'fault',id,name:'peer-off',value:+(value==='peer-off')});});
  $('magnetic').addEventListener('change',ev=>{this.clockFaults[this.target()]=ev.target.value==='uncertain';this.publishContacts();});
  for(const name of ['imu','module-left','module-right'])$(name).addEventListener('input',ev=>{this.diagnostics[this.target()]??={};this.diagnostics[this.target()][name]=ev.target.value;if(name==='imu')$('imu-out').textContent=ev.target.value+'°';});
  document.querySelectorAll('[data-scenario]').forEach(b=>b.addEventListener('click',()=>this.scenario(b.dataset.scenario)));
  $('restore').addEventListener('click',()=>this.restore());$('reset').addEventListener('click',async()=>{await this.send({op:'reset'});this.positions={A:{x:72,y:38},B:{x:646,y:56}};this.clockFaults={};this.render();this.syncPanel();});
  $('preview-audio').addEventListener('click',()=>this.audio.start(false).catch(e=>this.toast(e.message)));$('silent-start').addEventListener('click',()=>{this.audio.start(true);for(const id of this.physicalOrder)if(this.engine.devices[id].transport==='paused')this.send({op:'ui',id,action:3,value:0});});
  $('master-volume').addEventListener('input',ev=>{this.audio.volume=+ev.target.value/100;if(this.audio.gain)this.audio.gain.gain.value=this.audio.volume;$('master-value').textContent=ev.target.value+'%';});
  document.querySelectorAll('[data-language]').forEach(b=>b.addEventListener('click',()=>I.setLocale(b.dataset.language)));
  root.addEventListener('echoy:locale',()=>{this.locale();this.send({op:'locale',english:I.locale==='en'});I.apply(document.body);root.duijuGuide?.draw();root.duijuGuide?.refresh();document.querySelectorAll('[data-language]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.language===I.locale)));});
  document.addEventListener('visibilitychange',()=>{if(document.hidden)this.disconnect();});root.addEventListener('pagehide',()=>this.disconnect());
 }
 syncPanel(){const d=this.engine.devices[this.target()];$('battery').value=d.battery;$('battery-out').textContent=d.battery+'%';$('network').value=!d.linkOnline?'peer-off':d.wifi?'online':'wifi-off';$('magnetic').value=this.clockFaults[d.id]?'uncertain':'normal';}
 locale(){
  $('bridge-reconnect').textContent=tr('重新连接本地模拟器','Reconnect local simulator');
  $('firmware-mode').textContent=tr('固件行为模式 · 本地开发注入 · 3 × C core/control · LVGL 屏幕帧 · C DSP 声音。桌面接触为模拟，不能证明真实磁吸或声学同步。','Firmware behavior · Local development injection · 3 × C core/control · LVGL frames · C DSP audio. Simulated contact is not physical or acoustic evidence.');
  document.querySelector('.edition').textContent=tr('固件行为 · PC 开发模式','Firmware behavior · PC development');
  for(const [i,option]of [...$('target').options].entries())option.textContent=ids[i]+' · UID '+(100+i);
  document.documentElement.lang=I.locale;this.mapping();
 }
 render(){this.view.render();this.desk?.renderDock();$('empty-desk').hidden=this.physicalOrder.length>0;const d=this.engine.devices[this.target()];
  $('desk-state').textContent=`${this.physicalOrder.length} ${tr('台在桌上 · C 固件行为','devices on desk · C firmware behavior')}`;$('session-summary').textContent=tr('各台独立播放；稳定接触经实际协议协商后同场。','Play each device independently; stable contact starts protocol negotiation.');
  $('identity-readout').textContent=`UID ${d.uid||'—'} / boot ${d.boot||'—'} / revision ${d.revision||0}`;
  $('discovery-readout').textContent=`${d.linkOnline?tr('演奏无线正常','Peer radio online'):tr('演奏无线失联','Peer radio offline')} / Wi-Fi ${d.wifi?'online':'off'}`;
  $('clock-readout').textContent=`${d.members||1} members / ${this.answer?.time_us||0} µs / ${d.feedback||'ready'}`;
  $('contact-readout').textContent=JSON.stringify({observed:this.answer?.contacts||[],committed:Object.keys(this.engine.committedEdges),uncertain:this.answer?.uncertain||[]});
 }
 toast(message){I.setText($('toast'),message);$('toast').classList.add('show');clearTimeout(this.toastTimer);this.toastTimer=setTimeout(()=>$('toast').classList.remove('show'),3100);}
 mapping(){const rows=I.locale==='en'?[['480×480 pages / touch','echoy_view / echoy_touch · shared LVGL renderer','Fonts, touch and concurrent audio'],['Three devices / adjacency / sessions','Three echoy_control processes · C protocol / core','Physical peer identity, acoustic synchronization and 30-minute endurance'],['Four roles / twelve phrases / audio','echoy_music + echoy_audio → PCM → AudioWorklet','ES8311 output and speaker listening'],['Desk / guide / fault injection','Original HTML shell adapter · explicit PC development mode','Coordinates, fake time and simulated packets are not hardware evidence'],['Power / reboot / connection loss','C settings / boot generation / bounded protocol','Recovery and autonomous operation after computer disconnect']]:[['480×480 产品页面 / 触点','echoy_view / echoy_touch · 同一 LVGL renderer','字体、触摸和音频并发'],['三台播放 / 邻接 / 会话','三个 echoy_control 进程 · C protocol / core','真实磁吸对端识别、声学同步与30分钟连续演奏'],['四角色 / 十二乐句 / 声音','echoy_music + echoy_audio → PCM → AudioWorklet','ES8311 输出与扬声器听测'],['桌面 / 指引 / 故障注入','原 HTML 外壳适配器 · PC 显式开发模式','不能把坐标、假时钟和网络模拟当作真机证据'],['关机 / 重开 / 网络异常','C settings / boot 世代 / bounded protocol','Recovery 与独立运行，电脑断开不控制真机']];
  $('mapping-content').innerHTML='<table><thead><tr><th>'+tr('HTML 行为','HTML behavior')+'</th><th>'+tr('固件基础','Firmware')+'</th><th>'+tr('真机验收','Hardware acceptance')+'</th></tr></thead><tbody>'+rows.map(r=>'<tr>'+r.map(x=>'<td>'+root.duijuEscape(x)+'</td>').join('')+'</tr>').join('')+'</tbody></table><p>'+tr('Echoy · ESP-IDF 6.2 dev · PC 故障注入不进入普通固件。','Echoy · ESP-IDF 6.2 dev · PC injection is absent from ordinary firmware.')+'</p>';
 }
}
root.duiju=new App();
})(window);
