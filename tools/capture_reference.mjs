#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const oracleHash = 'd23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5';
const referencePath = path.join(root, 'reference', 'Echoy_v5.0.3.html');
const uiCasesPath = path.join(root, 'tests', 'cases', 'ui-cases.json');
const capturePath = path.join(root, 'tests', 'cases', 'ui-capture.json');
const manifestPath = path.join(root, 'reference', 'ui', 'manifest.json');
const chromePath = process.env.ECHOY_CHROME || '/usr/bin/google-chrome';

function sha256(data) {
  return crypto.createHash('sha256').update(data).digest('hex');
}

function stableJson(value) {
  return JSON.stringify(value, null, 2) + '\n';
}

function writeStable(file, data) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  if (!fs.existsSync(file) || !fs.readFileSync(file).equals(data)) fs.writeFileSync(file, data);
}

function wait(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

class CdpClient {
  constructor(socket) {
    this.socket = socket;
    this.serial = 0;
    this.pending = new Map();
    this.waiters = new Map();
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (!message.id) {
        const waiters = this.waiters.get(message.method);
        if (waiters?.length) waiters.shift()(message.params);
        return;
      }
      const pending = this.pending.get(message.id);
      if (!pending) return;
      this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(`${pending.method}: ${message.error.message}`));
      else pending.resolve(message.result);
    });
  }

  send(method, params = {}) {
    const id = ++this.serial;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject, method });
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }

  once(method) {
    return new Promise(resolve => {
      const waiters = this.waiters.get(method) || [];
      waiters.push(resolve);
      this.waiters.set(method, waiters);
    });
  }
}

async function connect(url) {
  const socket = new WebSocket(url);
  await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, { once: true });
    socket.addEventListener('error', reject, { once: true });
  });
  return new CdpClient(socket);
}

async function waitForDebugPort(profile, process) {
  const portFile = path.join(profile, 'DevToolsActivePort');
  for (let attempt = 0; attempt < 400; attempt++) {
    if (process.exitCode !== null) throw new Error(`Chrome exited before opening DevTools (code ${process.exitCode})`);
    if (fs.existsSync(portFile)) return fs.readFileSync(portFile, 'utf8').split(/\r?\n/)[0];
    await wait(25);
  }
  throw new Error('Chrome did not open a DevTools port within 10 seconds');
}

function makeHarness(html, uiCases) {
  const fontCss = uiCases.fontFaces.map(font =>
    `@font-face{font-family:${JSON.stringify(font.family)};font-style:${font.style};font-weight:${font.weight};src:url(${JSON.stringify(pathToFileURL(font.source).href)})}`
  ).join('');
  const head = `<style id="echoy-capture-style">${fontCss}
html,body{margin:0!important;width:480px!important;height:480px!important;overflow:hidden!important;background:#20231e!important}
body,button,input,select{font-family:"Echoy Latin","Echoy CJK",sans-serif!important}
.screen-label,.battery-value,.expression-value,.phrase-option>span{font-family:"Echoy Mono","Echoy CJK",monospace!important}
*,*:before,*:after{animation:none!important;transition:none!important;caret-color:transparent!important}
</style><script>
(()=>{const fixed=${uiCases.fixedTimeMs};Object.defineProperty(performance,'now',{configurable:true,value:()=>fixed});
const NativeDate=Date;class FrozenDate extends NativeDate{constructor(...args){super(...(args.length?args:[1700000000000]));}static now(){return 1700000000000;}}window.Date=FrozenDate;
window.setInterval=()=>0;window.clearInterval=()=>{};window.requestAnimationFrame=()=>0;window.cancelAnimationFrame=()=>{};
const nativeMatchMedia=window.matchMedia.bind(window);window.matchMedia=query=>query.includes('prefers-reduced-motion')?{matches:true,media:query,onchange:null,addListener(){},removeListener(){},addEventListener(){},removeEventListener(){},dispatchEvent(){return false;}}:nativeMatchMedia(query);
const nativeArc=CanvasRenderingContext2D.prototype.arc;CanvasRenderingContext2D.prototype.arc=function(x,y,r,...rest){if(window.__ECHOY_SCORE_CAPTURE_ACTIVE&&this.canvas?.classList.contains('instrument-canvas')&&Math.abs(y-210)<.0001)(window.__ECHOY_SCORE_ARCS__??=[]).push({x,y,r});return nativeArc.call(this,x,y,r,...rest);};
try{localStorage.clear();localStorage.setItem('duiju-guide-v041-dismissed','1');}catch(_){}})();
</script>`;
  const cases = JSON.stringify(uiCases.cases).replaceAll('</script', '<\\/script');
  const tail = `<script>
window.__ECHOY_CAPTURE_READY__=(async()=>{const item=${cases}[Number(new URLSearchParams(location.search).get('case')||0)];
const app=window.duiju,d=app.engine.devices.A;app.engine.tick(10.72);
d.role=item.role;d.phrase=item.phrase;d.activity=item.activity;d.committedActivity=item.committedActivity??item.activity;d.memories[item.role]=item.activity;d.phraseMemories[item.role]=item.phrase;d.volume=item.volume??.70;d.battery=item.battery??86;d.lifecycle='ready';d.motifAvailable=true;d.transport='paused';d.muted=true;d.loading=false;d.rolePreparing=null;d.fault=null;d.error=null;
app.routes.A=item.route;app.volumeOpen.A=false;app.playRequests={};app.audio.playing=false;app.notices={};
if(item.state==='booting'){d.lifecycle='booting';d.motifAvailable=false;}
else if(item.state==='playing'){d.transport='playing';d.muted=false;app.audio.playing=true;}
else if(item.state==='waiting'){d.transport='waiting';d.muted=true;}
else if(item.state==='volume'){app.volumeOpen.A=true;}
else if(item.state==='loading'){d.loading=true;d.motifAvailable=false;}
else if(item.state==='role-preparing'){d.rolePreparing=item.preparingRole;}
else if(item.state==='role-error'){d.error='素材不可用';}
else if(item.state==='data-error'){d.fault='data';d.motifAvailable=false;}
else if(item.state==='audio-error'){d.fault='audio';}
const shareSession=()=>{const a=app.engine.sessionFor('A'),b=app.engine.sessionFor('B');if(b&&b.id!==a.id)delete app.engine.sessions[b.id];a.members=['A','B'];app.engine.devices.B.sessionId=a.id;app.engine.devices.B.lifecycle='ready';app.engine.devices.B.motifAvailable=true;};
const setEdge=key=>{shareSession();app.engine.committedEdges={[key]:true};};
if(item.feedbackBranch==='motif-recovery')d.motifAvailable=false;
else if(item.feedbackBranch==='play-preparing')app.playRequests.A={boot:d.bootId};
else if(item.feedbackBranch==='sync-failed')app.engine.merges.fixture={ids:['A','B'],stage:'failed',applyAt:null};
else if(item.feedbackBranch==='syncing')app.engine.merges.fixture={ids:['A','B'],stage:'syncing',applyAt:null};
else if(item.feedbackBranch==='sync-waiting')app.engine.merges.fixture={ids:['A','B'],stage:'waiting',applyAt:app.engine.time+2};
else if(item.feedbackBranch==='pending-role')app.engine.pending.roles.A='harmony';
else if(item.feedbackBranch==='pending-phrase')app.engine.pending.phrases.A={role:'voice',value:2};
else if(item.feedbackBranch==='moving')app.engine.moving.A=app.engine.time;
else if(item.feedbackBranch==='contact-confirming')app.engine.contactInput={'A>B':{left:'A',right:'B',state:'uncertain',since:app.engine.time}};
else if(item.feedbackBranch==='arrangement-pending'){shareSession();const session=app.engine.sessionFor('A');app.engine.arrangements[session.id]={signature:'fixture',target:{'A>B':true},applyAt:app.engine.time+2,ids:['A','B']};}
else if(item.feedbackBranch==='notice')app.notices.A={text:'已生效',until:performance.now()+1100};
else if(item.feedbackBranch==='link-split'){d.linkOnline=false;d.lostSplit=true;}
else if(item.feedbackBranch==='link-hold'){d.linkOnline=false;d.lostSplit=false;}
else if(item.feedbackBranch==='normal-source'||item.feedbackBranch==='paused-source')setEdge('B>A');
else if(item.feedbackBranch==='normal-leader')setEdge('A>B');
else if(item.feedbackBranch==='normal-shared-solo'||item.feedbackBranch==='paused-shared')shareSession();
window.EchoyI18n.setLocale(item.locale);app.render();app.view.draw('A');await document.fonts.ready;app.view.draw('A');
const screen=app.view.units.A.screen,screenRect=screen.getBoundingClientRect(),round=n=>Math.round(n*1000)/1000;
const relativeRect=element=>{const r=element.getBoundingClientRect();return{x:round(r.x-screenRect.x),y:round(r.y-screenRect.y),width:round(r.width),height:round(r.height)};};
const props=['display','position','left','top','right','bottom','width','height','color','backgroundColor','fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','borderRadius','padding','gap','gridTemplateColumns','opacity','overflow','touchAction'];
const selectors=['.product-page','.top-status','.role-heading','.phrase-name','.role-subtitle','.instrument-canvas','.gesture-zone','.expression-control','.role-tab','.phrase-option','.volume-sheet','.status-symbol','.status-heading','.status-copy','.status-action','.bottom-nav','.device-button.secondary','.device-button.transport-button'];
const styles={};for(const selector of selectors){const elements=[...screen.querySelectorAll(selector)];if(elements.length)styles[selector]=elements.map(element=>{const computed=getComputedStyle(element),style={rect:relativeRect(element)};for(const prop of props)style[prop]=computed[prop];return style;});}
const actions=[...screen.querySelectorAll('[data-action]')].map(element=>{const computed=getComputedStyle(element);return{action:element.dataset.action,value:element.dataset.value??null,tag:element.tagName.toLowerCase(),text:element.textContent.replace(/\\s+/g,' ').trim(),ariaLabel:element.getAttribute('aria-label'),disabled:element.disabled,rect:relativeRect(element),computed:{display:computed.display,pointerEvents:computed.pointerEvents,touchAction:computed.touchAction}};});
const plan=app.plan().find(entry=>entry.id==='A'),motif=JSON.parse(JSON.stringify(plan?.motif||[])),canvas=screen.querySelector('canvas');let pixelHash=null,renderedEventSteps=[];if(canvas){const audioPlaying=app.audio.playing;window.__ECHOY_SCORE_ARCS__=[];window.__ECHOY_SCORE_CAPTURE_ACTIVE=true;app.audio.playing=false;app.view.draw('A');window.__ECHOY_SCORE_CAPTURE_ACTIVE=false;renderedEventSteps=window.__ECHOY_SCORE_ARCS__.filter(dot=>dot.r>2).map(dot=>Math.round((dot.x-29)/19));app.audio.playing=audioPlaying;app.view.draw('A');const bytes=canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;let value=2166136261;for(const byte of bytes){value^=byte;value=Math.imul(value,16777619);}pixelHash=(value>>>0).toString(16).padStart(8,'0');}
const feedbackElement=screen.querySelector('[data-feedback]');
const result={id:item.locale+'/'+item.id,locale:item.locale,state:item.state,route:screen.querySelector('.product-page')?.dataset.route??null,screen:{width:round(screenRect.width),height:round(screenRect.height),text:screen.innerText.replace(/[ \\t]+/g,' ').replace(/\\n{2,}/g,'\\n').trim()},feedback:{branch:item.feedbackBranch??null,text:feedbackElement?.textContent.trim()??null},actions,styles,motif,canvas:{width:canvas?.width??0,height:canvas?.height??0,pixelHash,observation:canvas?'CanvasRenderingContext2D.arc calls at score y=210':null,renderedEventSteps}};
document.body.replaceChildren(screen);screen.style.position='absolute';screen.style.inset='0';screen.style.margin='0';window.__ECHOY_CAPTURE__=result;return result;})();
</script>`;
  return html.replace('</head>', `${head}</head>`).replace('</body>', `${tail}</body>`);
}

async function captureAll(uiCases, writeImages) {
  assert(fs.existsSync(chromePath), `Chrome not found: ${chromePath}`);
  const versionResult = spawnSync(chromePath, ['--version'], { encoding: 'utf8' });
  assert.equal(versionResult.status, 0, versionResult.stderr);
  const browserVersion = versionResult.stdout.trim();
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'echoy-reference-'));
  const profile = path.join(temp, 'profile');
  const harnessPath = path.join(temp, 'capture.html');
  fs.writeFileSync(harnessPath, makeHarness(fs.readFileSync(referencePath, 'utf8'), uiCases));
  const browser = spawn(chromePath, [
    '--headless=new', '--no-sandbox', '--disable-dev-shm-usage', '--disable-gpu',
    '--disable-lcd-text', '--font-render-hinting=none', '--force-color-profile=srgb',
    '--force-device-scale-factor=1', '--hide-scrollbars', '--window-size=480,480',
    '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank',
  ], { stdio: ['ignore', 'ignore', 'ignore'] });

  try {
    const port = await waitForDebugPort(profile, browser);
    let pages;
    for (let attempt = 0; attempt < 100; attempt++) {
      pages = await fetch(`http://127.0.0.1:${port}/json/list`).then(response => response.json());
      if (pages.some(page => page.type === 'page')) break;
      await wait(20);
    }
    const page = pages.find(item => item.type === 'page');
    assert(page, 'Chrome did not expose a page target');
    const cdp = await connect(page.webSocketDebuggerUrl);
    await cdp.send('Page.enable');
    await cdp.send('Runtime.enable');
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 480, height: 480, deviceScaleFactor: 1, mobile: false });
    await cdp.send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }] });

    const captures = [];
    const images = [];
    for (let index = 0; index < uiCases.cases.length; index++) {
      const item = uiCases.cases[index];
      const url = `${pathToFileURL(harnessPath).href}?case=${index}`;
      const loaded = cdp.once('Page.loadEventFired');
      await cdp.send('Page.navigate', { url });
      await loaded;
      const evaluated = await cdp.send('Runtime.evaluate', {
        expression: 'window.__ECHOY_CAPTURE_READY__',
        awaitPromise: true,
        returnByValue: true,
      });
      if (evaluated.exceptionDetails) throw new Error(`capture ${item.locale}/${item.id}: ${evaluated.exceptionDetails.text}`);
      const capture = evaluated.result.value;
      assert(capture, `capture ${item.locale}/${item.id} returned no metadata`);
      const screenshot = await cdp.send('Page.captureScreenshot', {
        format: 'png',
        fromSurface: true,
        captureBeyondViewport: false,
        clip: { x: 0, y: 0, width: 480, height: 480, scale: 1 },
      });
      const bytes = Buffer.from(screenshot.data, 'base64');
      assert.equal(bytes.readUInt32BE(16), 480);
      assert.equal(bytes.readUInt32BE(20), 480);
      const relative = `reference/ui/${item.locale}/${item.id}.png`;
      if (writeImages) writeStable(path.join(root, relative), bytes);
      captures.push(capture);
      images.push({ id: `${item.locale}/${item.id}`, path: relative, size: bytes.length, width: 480, height: 480, sha256: sha256(bytes) });
    }
    cdp.socket.close();
    const fonts = uiCases.fontFaces.map(font => ({ ...font, size: fs.statSync(font.source).size, sha256: sha256(fs.readFileSync(font.source)) }));
    return {
      capture: { schemaVersion: 1, oracleSha256: oracleHash, caseCount: captures.length, browser: { path: chromePath, version: browserVersion }, fonts, cases: captures },
      manifest: { schemaVersion: 1, oracleSha256: oracleHash, caseCount: images.length, browser: { path: chromePath, version: browserVersion }, images },
    };
  } finally {
    if (browser.exitCode === null) browser.kill('SIGTERM');
    fs.rmSync(temp, { recursive: true, force: true });
  }
}

async function main() {
  assert.equal(sha256(fs.readFileSync(referencePath)), oracleHash, 'reference SHA-256 changed');
  const uiCases = JSON.parse(fs.readFileSync(uiCasesPath, 'utf8'));
  assert.equal(uiCases.oracleSha256, oracleHash);
  for (const font of uiCases.fontFaces) assert(fs.existsSync(font.source), `frozen font not found: ${font.source}`);
  const generate = process.argv.includes('--generate');
  const check = process.argv.includes('--check');
  if (generate === check) throw new Error('usage: node tools/capture_reference.mjs --generate | --check');
  const generated = await captureAll(uiCases, generate);
  if (check) {
    assert.equal(fs.readFileSync(capturePath, 'utf8'), stableJson(generated.capture), 'UI capture metadata differs');
    assert.equal(fs.readFileSync(manifestPath, 'utf8'), stableJson(generated.manifest), 'UI image manifest differs');
    const committedManifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    for (const image of generated.manifest.images) {
      const committed = committedManifest.images.find(item => item.id === image.id);
      assert(fs.existsSync(path.join(root, image.path)), `missing UI baseline: ${image.path}`);
      assert.equal(sha256(fs.readFileSync(path.join(root, image.path))), committed.sha256, `${image.id} committed screenshot changed`);
      assert.equal(image.sha256, committed.sha256, `${image.id} screenshot differs`);
    }
  } else {
    writeStable(capturePath, Buffer.from(stableJson(generated.capture)));
    writeStable(manifestPath, Buffer.from(stableJson(generated.manifest)));
  }
  process.stdout.write(stableJson({ ok: true, mode: check ? 'check' : 'generate', uiCases: generated.capture.caseCount, browser: generated.capture.browser.version }));
}

await main();
