#!/usr/bin/env python3
"""Launch three official GSP hosts and the Echoy C desk; serve loopback controls.

The frozen prototype supplies the outer desk geometry and spatial rules. Device
screens are unmodified official simulator iframes. No audio is generated in JS.
Use --headless --silent with temporary ports for unattended integration checks.
"""
import argparse
import hashlib
from http.server import ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import urlsplit
import webbrowser

from preview_controls import MAX_JSON, ControlDirectory, Handler as ProtectedHandler

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "projects/echoy"
COMPONENT = APP / "managed_components/espressif__esp-gsp"
REFERENCE = ROOT / "reference/v4/echoy-arrange-B-v4.html"
TILES = ("A", "B", "C")


def native_command(value):
    """Validate a bounded structured request before encoding the native protocol."""
    if not isinstance(value, dict):
        raise ValueError("command must be an object")
    action = value.get("action")
    keys = {"present": {"action", "tile", "present"},
            "edges": {"action", "edges"}, "condition": {"action", "tile", "condition"},
            "volume": {"action", "tile", "volume"}, "example": {"action", "tile"},
            "pause_all": {"action"}, "export": {"action"}, "motion": {"action", "reduce"}}
    if not isinstance(action, str) or action not in keys or set(value) != keys[action]:
        raise ValueError("unknown command fields")
    tile = value.get("tile")
    if "tile" in value and tile not in TILES:
        raise ValueError("unknown tile")
    if action == "motion":
        if type(value["reduce"]) is not bool:
            raise ValueError("reduce must be boolean")
        return f"motion {int(value['reduce'])}"
    if action == "present":
        if type(value["present"]) is not bool:
            raise ValueError("present must be boolean")
        return f"present {tile} {int(value['present'])}"
    if action == "condition":
        if value["condition"] not in ("normal", "fault", "loading", "empty"):
            raise ValueError("unknown condition")
        return f"condition {tile} {value['condition']}"
    if action == "volume":
        volume = value["volume"]
        if type(volume) not in (int, float) or not math.isfinite(volume) or not 0 <= volume <= 1:
            raise ValueError("volume outside 0..1")
        return f"volume {tile} {volume:.8g}"
    if action == "example":
        return f"example {tile}"
    if action == "edges":
        edges = value["edges"]
        if not isinstance(edges, list) or len(edges) > 2:
            raise ValueError("at most two edges")
        successors, incoming, encoded = {}, set(), []
        for edge in edges:
            if not isinstance(edge, dict) or set(edge) != {"left", "right"}:
                raise ValueError("invalid edge")
            left, right = edge["left"], edge["right"]
            if left not in TILES or right not in TILES or left == right or left in successors or right in incoming:
                raise ValueError("invalid edge topology")
            successors[left] = right; incoming.add(right); encoded.extend((left, right))
        for start in successors:
            seen, current = set(), start
            while current in successors:
                if current in seen:
                    raise ValueError("cyclic edges")
                seen.add(current); current = successors[current]
        return " ".join(["edges", *encoded])
    return action


class DeskControls(ControlDirectory):
    def status_snapshot(self):
        # Read identity and JSON from the same opened file. The native writer
        # publishes via rename, so a later path lookup could name a different
        # generation from the content we actually read.
        fd = os.open("status.json", os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=self.fd)
        try:
            details = os.fstat(fd)
            if not stat.S_ISREG(details.st_mode) or details.st_uid != os.getuid() or details.st_mode & 0o077 or details.st_size > MAX_JSON:
                raise ValueError("invalid desk status file")
            with os.fdopen(fd, "rb", closefd=False) as file:
                payload = file.read(MAX_JSON + 1)
            if len(payload) > MAX_JSON:
                raise ValueError("oversized desk status")
            saved = json.loads(payload)
            if not isinstance(saved, dict) or type(saved.get("ready")) is not bool or not isinstance(saved.get("tiles"), dict):
                raise ValueError("invalid desk status")
            generation = (details.st_dev, details.st_ino, details.st_mtime_ns, details.st_ctime_ns, details.st_size)
            return saved, generation
        finally:
            os.close(fd)

    def command_socket_exists(self):
        try:
            os.stat("command.sock", dir_fd=self.fd, follow_symlinks=False)
            return True
        except FileNotFoundError:
            return False

    def status(self, previous_generation=None):
        result = {"ready": False, "audio_connected": False, "tiles": {}, "edges": [],
                  "export_available": False, "export_counter": 0}
        try:
            saved, generation = self.status_snapshot()
            if generation != previous_generation:
                result.update(saved)
        except (OSError, ValueError):
            pass
        try:
            with self.transport():
                result["connected"] = True
        except (OSError, ValueError):
            result["connected"] = False
        return result

    def send(self, value):
        message = native_command(value).encode("ascii")
        with self.transport() as connection:
            connection.send(message)


def reference_text():
    data = REFERENCE.read_bytes()
    if hashlib.sha256(data).hexdigest() != "c7e3e2e6992dc1f21857bfa6a8dc1357b5f95ea2dfad0425f60e83b1085ee7e3":
        raise ValueError("frozen reference checksum mismatch")
    return data.decode()


def spatial_script():
    text = reference_text()
    return text[text.index("class DuijuSpatial{"):text.index("root.DuijuSpatial=DuijuSpatial;")]


def styles():
    original = reference_text().split("<style>", 1)[1].split("</style>", 1)[0]
    return (original + """
.desk-controls{max-width:1752px;margin:0 auto 24px;padding:16px 24px;border:1px solid #d4ddc9;border-radius:12px;background:#f1f3e9;color:#6b7d59}
.desk-controls button,.desk-controls select{font:inherit;min-height:36px;border:1px solid #c6d2b7;border-radius:7px;padding:6px 11px;background:#e8eedf;color:#60764a}
.desk-status{font-size:13px;line-height:1.8;min-height:24px}.desk-controls .row-controls{display:flex;flex-wrap:wrap;align-items:center;gap:10px}
.desk-controls a{color:#526646}.unit .screen iframe{position:absolute;left:-260px;top:-62px;width:1000px;height:700px;border:0;max-width:none}
.unit .screen{overflow:hidden}.device-handle{gap:10px}.device-handle .handle-name{flex:1}.put-away{font-size:11px;color:#738263;border:0;background:transparent;padding:4px;cursor:pointer}
.unit.dragging iframe{pointer-events:none}.seam-mark{z-index:5}.desk-error{color:#a14e32;min-height:22px;margin:0;font-size:13px}
.shelf-tile:focus-visible{outline:2px solid #8f9c7d}.native-link{font-size:11px;white-space:nowrap}.stage-empty{position:absolute;left:60px;top:180px;color:#809070;font-size:20px}
""").encode()


def page(urls):
    units, shelf, links = [], [], []
    for tile, url in zip(TILES, urls):
        units.append(f'''<section class="unit" data-tile="{tile}" hidden>
<div class="device-handle" role="button" tabindex="0" aria-label="移动设备 {tile}"><span class="handle-name">{tile}</span><button class="put-away" data-remove="{tile}" aria-label="收起设备 {tile}">收起</button><span class="handle-grip">⠿</span></div>
<div class="shell"><div class="screen"><iframe src="{url}" title="{tile} · 官方 GSP 设备画面" scrolling="no"></iframe><i class="seam-mark left"></i><i class="seam-mark right"></i></div><i class="hardware ai"></i><i class="hardware boot"></i><i class="hardware power"></i><i class="usb"></i></div></section>''')
        shelf.append(f'<button class="shelf-tile" data-shelf="{tile}"><strong>{tile}</strong><span>拖上桌 · 初始静音</span></button>')
        links.append(f'<a class="native-link" href="{url}" target="_blank" rel="noopener">{tile} 原生预览</a>')
    return ('''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>合拍 · 三台模拟桌面</title><link rel="stylesheet" href="/style.css"><script src="/app.js" defer></script></head><body>
<header class="mast"><div class="wordmark">ECHOY / 合拍 · PC SIMULATION</div><h1>先敲一段，再把声音摆在一起。</h1><p>点「录制」，两拍预备后录四拍；先听一次，喜欢再点 ▶。把另一台放到右侧，再点它自己的 ▶。</p><div class="quick-start"><button id="example">先听“两下紧挨”的示例</button><button id="pause-all">全部暂停</button></div></header>
<main class="layout"><section class="bench arrange-bench"><div class="bench-top"><span>合拍 · A / B / C</span><span>480 × 480 / 每台独立操作</span></div><div class="viewport" id="viewport"><div id="stage"><div class="stage-empty" id="empty-desk">从下方拿一台，放到桌上。</div><div id="snap-ghost" hidden></div>''' + "".join(units) + '''</div></div><div class="bench-foot">拖上方外壳移动 · 拆开不清空、不强制暂停 · 每台单独发声</div><div class="shelf" id="shelf"><h2>再拿一台 · 拖到桌上</h2>''' + "".join(shelf) + '''</div></section>
<section class="desk-controls"><div class="desk-status" id="audio-status" role="status">正在连接原生桌面…</div><p class="desk-error" id="error" role="alert"></p><div class="row-controls"><label>模拟设备 <select id="selected"><option>A</option><option>B</option><option>C</option></select></label><button data-condition="fault">模拟音频故障</button><button data-condition="loading">模拟加载</button><button data-condition="empty">模拟电量耗尽</button><button data-condition="normal">恢复到静止</button><button id="export">导出本次节奏</button>''' + "".join(links) + '''</div><p class="desk-status">PC 三台共享音频时钟与本地联动；不代表无线同步或真机验收。</p></section></main></body></html>''').encode()


SCRIPT = r"""
'use strict';
const ids=['A','B','C'], names={pulse:'脉冲',voice:'声线',dust:'微尘',harmony:'和声'};
const spatial=new DuijuSpatial(),positions={A:{x:32,y:22}},intents={},units={},shelf={};
let status=null,contactTimer=null,dragging=false,exportTarget=null,geometryRevision=0,requestQueue=Promise.resolve();
const boots={};
const reducedMotion=window.matchMedia('(prefers-reduced-motion: reduce)');
let sentMotion=null;
async function syncMotion(){if(status?.connected&&sentMotion!==reducedMotion.matches){const value=reducedMotion.matches;await command({action:'motion',reduce:value});sentMotion=value;}}
reducedMotion.addEventListener('change',()=>{sentMotion=null;syncMotion().catch(report);});
const error=document.getElementById('error'),viewport=document.getElementById('viewport'),stage=document.getElementById('stage');
for(const id of ids){units[id]=document.querySelector(`[data-tile="${id}"]`);shelf[id]=document.querySelector(`[data-shelf="${id}"]`);}
function command(body,valid=()=>true){const task=requestQueue.catch(()=>{}).then(async()=>{if(!valid())return false;const r=await fetch('/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),keepalive:true});if(!r.ok)throw Error('原生桌面未接收操作，请检查连接。');return true;});requestQueue=task;return task;}
function report(e){error.textContent=e.message||String(e);}
function select(id){document.getElementById('selected').value=id;}
function freePosition(){let p={x:32,y:22};while(spatial.collision('_new',p,positions)){p.x+=560;if(p.x>1400){p.x=32;p.y+=588;}}return p;}
function layout(){for(const id of ids){const p=positions[id];units[id].hidden=!p;shelf[id].hidden=!!p;if(p){units[id].style.left=p.x+'px';units[id].style.top=p.y+'px';}}document.getElementById('empty-desk').hidden=Object.keys(positions).length>0;seams();}
function seams(){const proposed=spatial.edges(positions);for(const id of ids)for(const side of ['left','right']){const find=edges=>edges.find(e=>e[side==='left'?'right':'left']===id),old=find(status?.edges||[]),want=find(proposed);const same=old&&want&&old.left===want.left&&old.right===want.right;units[id].querySelector('.seam-mark.'+side).className='seam-mark '+side+(same?' connected':old||want?' pending':'');}}
function confirmGeometry(){clearTimeout(contactTimer);const revision=++geometryRevision;seams();if(Object.values(boots).some(boot=>boot.loading))return;contactTimer=setTimeout(()=>{command({action:'edges',edges:spatial.edges(positions).map(({left,right})=>({left,right}))},()=>revision===geometryRevision).catch(report);},450);}
async function present(id,value,p){
select(id);const old=positions[id];clearTimeout(boots[id]?.timer);clearTimeout(contactTimer);++geometryRevision;
const boot={loading:value,timer:null};boots[id]=boot;const current=()=>boots[id]===boot;
if(value)positions[id]=p||freePosition();else delete positions[id];intents[id]={value,until:Date.now()+5000};layout();
try{
  await command({action:'present',tile:id,present:value},current);if(!current())return;
  if(!value){confirmGeometry();return;}
  await command({action:'condition',tile:id,condition:'loading'},current);if(!current())return;
  boot.timer=setTimeout(async()=>{try{await command({action:'condition',tile:id,condition:'normal'},current);if(!current())return;boot.loading=false;confirmGeometry();}catch(e){if(current()){boot.loading=false;report(e);}}},220);
}catch(e){if(!current())return;boot.loading=false;delete intents[id];if(old)positions[id]=old;else delete positions[id];layout();report(e);}}
function bindDrag(el,id,reserve){let drag=null;
el.addEventListener('pointerdown',e=>{if(e.target.closest('[data-remove]')||e.button!==0||e.isPrimary===false)return;e.preventDefault();el.setPointerCapture(e.pointerId);select(id);const bounds=stage.getBoundingClientRect(),p=positions[id];drag={id:e.pointerId,x:e.clientX,y:e.clientY,offsetX:reserve?254:e.clientX-bounds.x-p.x,offsetY:reserve?20:e.clientY-bounds.y-p.y,moved:false};dragging=true;units[id].classList.add('dragging');});
el.addEventListener('pointermove',e=>{if(!drag||drag.id!==e.pointerId)return;if(Math.abs(e.clientX-drag.x)+Math.abs(e.clientY-drag.y)>5)drag.moved=true;if(!drag.moved)return;const v=viewport.getBoundingClientRect();if(e.clientX>v.right-38)viewport.scrollLeft+=25;if(e.clientX<v.left+38)viewport.scrollLeft-=25;if(e.clientY>v.bottom-30)viewport.scrollTop+=18;if(e.clientY<v.top+30)viewport.scrollTop-=18;const bounds=stage.getBoundingClientRect();drag.point={x:Math.max(12,Math.min(1850-508,e.clientX-bounds.x-drag.offsetX)),y:Math.max(12,Math.min(1160-566,e.clientY-bounds.y-drag.offsetY))};drag.snap=spatial.snap(id,drag.point,positions);const ghost=document.getElementById('snap-ghost');ghost.hidden=!drag.snap.snapped;ghost.style.left=drag.snap.x+'px';ghost.style.top=drag.snap.y+'px';if(!reserve){units[id].style.left=drag.point.x+'px';units[id].style.top=drag.point.y+'px';}});
const end=(e,cancel)=>{if(!drag||drag.id!==e.pointerId)return;document.getElementById('snap-ghost').hidden=true;units[id].classList.remove('dragging');if(!cancel&&drag.moved&&drag.point){const v=viewport.getBoundingClientRect(),inside=e.clientX>=v.x&&e.clientX<=v.right&&e.clientY>=v.y&&e.clientY<=v.bottom,p=drag.snap?.snapped?{x:drag.snap.x,y:drag.snap.y}:drag.point;if(inside&&!spatial.collision(id,p,positions)){if(reserve)present(id,true,p);else{positions[id]=p;confirmGeometry();}}else if(!reserve){const dock=document.getElementById('shelf').getBoundingClientRect();if(e.clientY>=dock.y&&e.clientY<=dock.bottom)present(id,false);}}drag=null;dragging=false;layout();};
el.addEventListener('pointerup',e=>end(e,false));el.addEventListener('pointercancel',e=>end(e,true));el.addEventListener('lostpointercapture',e=>{if(drag)end(e,true);});
el.addEventListener('keydown',e=>{if(e.target.closest('[data-remove]'))return;if(reserve&&['Enter',' '].includes(e.key)){e.preventDefault();present(id,true);return;}const deltas={ArrowLeft:[-32,0],ArrowRight:[32,0],ArrowUp:[0,-32],ArrowDown:[0,32]};if(reserve||!deltas[e.key])return;e.preventDefault();const [dx,dy]=deltas[e.key],old=positions[id],snap=spatial.snap(id,{x:Math.max(12,Math.min(1342,old.x+dx)),y:Math.max(12,Math.min(594,old.y+dy))},positions);if(!snap.collision){positions[id]={x:snap.x,y:snap.y};layout();confirmGeometry();}});}
for(const id of ids){bindDrag(units[id].querySelector('.device-handle'),id,false);bindDrag(shelf[id],id,true);units[id].querySelector('[data-remove]').onclick=()=>present(id,false);}
document.querySelectorAll('[data-condition]').forEach(button=>button.onclick=()=>command({action:'condition',tile:document.getElementById('selected').value,condition:button.dataset.condition}).catch(report));
document.getElementById('example').onclick=()=>command({action:'example',tile:document.getElementById('selected').value}).catch(report);
document.getElementById('pause-all').onclick=()=>command({action:'pause_all'}).catch(report);
document.getElementById('export').onclick=async()=>{try{exportTarget={counter:(status?.export_counter||0)+1,until:Date.now()+8000};await command({action:'export'});}catch(e){exportTarget=null;report(e);}};
document.addEventListener('keydown',e=>{if(e.key==='Escape')command({action:'pause_all'}).catch(report);});
document.addEventListener('visibilitychange',()=>{if(document.hidden)command({action:'pause_all'}).catch(report);});
window.addEventListener('pagehide',()=>command({action:'pause_all'}).catch(()=>{}));
async function refresh(){try{const r=await fetch('/status');if(!r.ok)throw Error('无法读取原生桌面状态');status=await r.json();await syncMotion();document.getElementById('audio-status').textContent=!status.connected?'原生桌面已断开':!status.ready?'三台设备正在准备…':status.audio_connected?'音频输出已连接 · 三台共用本地时钟':'音频输出未连接 · 当前不会发声';
if(!dragging)for(const id of ids){const tile=status.tiles?.[id];if(!tile)continue;const intent=intents[id];if(intent&&tile.present===intent.value)delete intents[id];else if(intent&&Date.now()<intent.until)continue;else if(intent){delete intents[id];report(Error('设备状态尚未确认，请重试。'));}if(tile.present&&!positions[id])positions[id]=freePosition();if(!tile.present)delete positions[id];const role=names[tile.role]||tile.role||id;units[id].querySelector('.handle-name').textContent=id+' · '+role;shelf[id].querySelector('strong').textContent=id+' · '+role;}
layout();if(exportTarget&&status.export_available&&status.export_counter>=exportTarget.counter){exportTarget=null;const a=document.createElement('a');a.href='/session.json';a.download='echoy-arrange-B-session.json';a.click();}else if(exportTarget&&Date.now()>exportTarget.until){exportTarget=null;report(Error('导出未完成，请重试。'));}
}catch(e){report(e);}setTimeout(refresh,350);}layout();refresh();
"""


def valid_urls(urls):
    if len(urls) != 3:
        raise ValueError("exactly three preview URLs required")
    for url in urls:
        parsed = urlsplit(url)
        if parsed.scheme != "http" or parsed.hostname != "127.0.0.1" or not parsed.port or parsed.path not in ("", "/") or parsed.query or parsed.fragment or parsed.username:
            raise ValueError("preview URLs must be plain IPv4 loopback HTTP origins")
    return urls


class Handler(ProtectedHandler):
    def reply(self, status, payload=b"", content_type="application/json; charset=utf-8", attachment=False):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        frames = " ".join(url.rstrip("/") for url in self.server.urls)
        self.send_header("Content-Security-Policy", f"default-src 'self'; style-src 'self' 'unsafe-inline'; frame-src {frames}; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        if attachment:
            self.send_header("Content-Disposition", 'attachment; filename="echoy-arrange-B-session.json"')
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if not self.allowed():
            return
        resources = {"/": (self.server.page, "text/html; charset=utf-8"),
                     "/style.css": (self.server.style, "text/css; charset=utf-8"),
                     "/app.js": (self.server.script, "text/javascript; charset=utf-8")}
        if self.path in resources:
            self.reply(200, *resources[self.path])
        elif self.path == "/status":
            self.reply(200, json.dumps(self.server.controls.status()).encode())
        elif self.path == "/session.json":
            try:
                payload = self.server.controls.session()
            except (OSError, ValueError):
                self.reply(404, b'{"error":"no exported session"}')
            else:
                self.reply(200, payload, attachment=True)
        else:
            self.reply(404, b'{"error":"unknown path"}')

    def do_POST(self):
        if not self.allowed(require_origin=True):
            return
        self.close_connection = True
        if self.path != "/command":
            self.reply(404); return
        length = self.headers.get("Content-Length", "")
        if not length.isdecimal() or self.headers.get("Transfer-Encoding"):
            self.reply(400); return
        if len(length) > 4 or int(length) > 1024:
            self.reply(413); return
        if self.headers.get("Content-Type", "").split(";", 1)[0].strip() != "application/json":
            self.reply(415); return
        try:
            value = json.loads(self.rfile.read(int(length)))
            native_command(value)
        except (ValueError, OSError):
            self.reply(400, b'{"error":"invalid command"}'); return
        try:
            self.server.controls.send(value)
        except (OSError, ValueError):
            self.reply(503, b'{"error":"native desk unavailable"}')
        else:
            self.reply(202, b'{"accepted":true}')


def make_server(controls, port, urls):
    urls = valid_urls(urls)
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.controls, server.urls = controls, urls
    server.page, server.style = page(urls), styles()
    server.script = (spatial_script() + SCRIPT).encode()
    return server


class OwnedProcesses:
    def __init__(self):
        self.children, self.logs = [], []

    def start(self, command, log):
        stream = open(log, "wb")
        self.logs.append(stream)
        child = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        self.children.append(child)
        return child

    def close(self):
        # Every process group was created here. Never search/kill by executable
        # name, listening port or files left by a previous launcher.
        for child in reversed(self.children):
            if child.poll() is None:
                try:
                    os.killpg(child.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        deadline = time.monotonic() + 5
        for child in reversed(self.children):
            try:
                child.wait(timeout=max(.01, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait()
        for stream in self.logs:
            stream.close()


def wait_ready(child, path, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if child.poll() is not None:
            raise RuntimeError(f"owned simulator exited ({child.returncode}); inspect its log")
        try:
            result = json.loads(path.read_text())
            if result.get("pid") != child.pid:
                raise RuntimeError("ready file belongs to another process")
            if not result.get("backend", "").startswith("tcp://127.0.0.1:"):
                raise RuntimeError("host did not provide a loopback backend")
            return result
        except (FileNotFoundError, json.JSONDecodeError):
            time.sleep(.025)
    raise TimeoutError("simulator readiness timed out")


def build(args):
    if args.no_build:
        return
    subprocess.run([sys.executable, str(COMPONENT / "tools/sim_bridge/build.py"),
                    "--project", str(APP / "pc"), "--component-dir", str(COMPONENT),
                    "--build-dir", str(args.build_dir), "--target", "echoy_backend",
                    "--gspc", str(args.gspc)], cwd=ROOT, check=True)
    subprocess.run(["cmake", "--build", str(args.build_dir), "--target", "echoy_desk", "--parallel"], check=True)


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=18896)
    parser.add_argument("--visual-ports", default="3222,3223,3224")
    parser.add_argument("--control-dir", type=Path)
    parser.add_argument("--build-dir", type=Path, default=APP / "build-sim-desk")
    parser.add_argument("--sim", type=Path, default=ROOT.parent / ".toolchains/gsp-sim-1.5.1/sim")
    parser.add_argument("--gspc", type=Path, default=ROOT.parent / ".toolchains/gspc-0.6.1/gspc")
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--duration-ms", type=int, default=0)
    parser.add_argument("--capture", type=Path, help="record the native audio adapter's 24kHz mono PCM to a WAV file")
    parser.add_argument("--silent", action="store_true")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--serve-only", action="store_true", help="serve an existing native control directory without owning processes")
    args = parser.parse_args(argv)
    try:
        args.visual_ports = [int(port) for port in args.visual_ports.split(",")]
        if len(args.visual_ports) != 3 or len(set(args.visual_ports)) != 3 or any(not 1 <= p <= 65535 for p in args.visual_ports):
            raise ValueError()
    except ValueError:
        parser.error("--visual-ports requires three distinct ports in 1..65535")
    if not 0 <= args.port <= 65535 or args.duration_ms < 0:
        parser.error("invalid HTTP port or duration")
    if args.serve_only and not args.control_dir:
        parser.error("--serve-only requires --control-dir")
    args.build_dir = args.build_dir.resolve()
    args.backend = args.backend or args.build_dir / "echoy_desk"
    args.bundle = args.bundle or args.build_dir / "echoy_backend_assets/app.gspb"
    return args


def main(argv=None):
    args = arguments(argv)
    owned = OwnedProcesses()
    control_path = args.control_dir or Path(tempfile.mkdtemp(prefix="echoy-desk-"))
    controls, server = None, None
    try:
        controls = DeskControls(control_path)
        urls = [f"http://127.0.0.1:{port}/" for port in args.visual_ports]
        # Fail on an occupied desk HTTP port before launching anything.
        server = make_server(controls, args.port, urls)
        backend = None
        if not args.serve_only:
            if controls.command_socket_exists():
                raise RuntimeError("control directory already contains a command socket; use a fresh directory")
            try:
                _, previous_generation = controls.status_snapshot()
            except FileNotFoundError:
                previous_generation = None
            build(args)
            if not args.backend.is_file() or not args.bundle.is_file():
                raise RuntimeError("missing echoy_desk executable or scene bundle")
            logs = Path(tempfile.mkdtemp(prefix="desk-run-", dir=control_path))
            endpoints = []
            for tile, port in zip(TILES, args.visual_ports):
                ready = logs / f"{tile}-ready.json"
                command = [str(args.sim), "--bundle", str(args.bundle), "--api-json", str(args.bundle.with_name("bundle_gsp.api.json")),
                           "--frames", "0", "--fps", "1000", "--idle-poll-ms", "1", "--backend-enable", "--backend-required", "--ready-file", str(ready)]
                command += ["--headless"] if args.headless else ["--visual-listen", f"127.0.0.1:{port}"]
                host = owned.start(command, logs / f"{tile}-host.log")
                endpoints += [f"--endpoint-{tile}", wait_ready(host, ready)["backend"]]
            command = [str(args.backend), *endpoints, "--control-dir", str(control_path)]
            if args.duration_ms:
                command += ["--duration-ms", str(args.duration_ms)]
            if args.silent:
                command.append("--silent")
            if args.capture:
                command += ["--capture", str(args.capture.resolve())]
            backend = owned.start(command, logs / "backend.log")
            deadline = time.monotonic() + 45
            while True:
                current_status = controls.status(previous_generation)
                if current_status.get("ready") and current_status.get("connected"):
                    break
                if any(child.poll() is not None for child in owned.children):
                    raise RuntimeError(f"native desk initialization failed; inspect {logs}")
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"native desk readiness timed out; inspect {logs}")
                time.sleep(.05)
        stop = threading.Event()
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        url = f"http://127.0.0.1:{server.server_port}"
        print(f"ECHOY_DESK_READY {url} control_dir={control_path}", flush=True)
        if not args.no_browser and not args.headless:
            webbrowser.open(url)
        try:
            while not stop.wait(.1):
                if backend and backend.poll() is not None:
                    if backend.returncode:
                        raise RuntimeError(f"native desk exited ({backend.returncode})")
                    break
                if any(child is not backend and child.poll() is not None for child in owned.children):
                    raise RuntimeError("an owned simulator exited")
        finally:
            server.shutdown(); worker.join()
        return 0
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"desk_preview: {error}", file=sys.stderr)
        return 1
    finally:
        owned.close()
        if server:
            server.server_close()
        if controls:
            controls.close()


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    raise SystemExit(main())
