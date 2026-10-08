#!/usr/bin/env python3
"""Local desk controls around the unmodified official GSP web preview.

Create a private directory (mode 0700) and pass it as ECHOY_SIM_CONTROL_DIR
to the native backend and --control-dir to this server. This script neither
launches nor stops the official simulator/CLI. It uses Python's standard library.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import socket
import stat

MAX_JSON = 256 * 1024
ACTIONS = frozenset({"restore", "fault", "loading", "recover", "empty"})

PAGE = """<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>合拍 · PC 模拟桌面</title><link rel="stylesheet" href="/style.css"><script src="/app.js" defer></script></head>
<body><main><header><p class="eyebrow">ECHOY · PC SIMULATION</p><h1>合拍 · 模拟桌面</h1>
<p>在设备画面中录制和切换表达；桌面上的控件用于模拟设备状态。</p></header>
<div class="desk"><section class="device" aria-label="设备预览">
<iframe id="preview" src="http://127.0.0.1:3222/" title="官方 GSP 设备预览"></iframe>
<div id="shelf" hidden><p>设备已收起，节奏仍然保留。</p><button data-action="restore">放回设备</button><p class="hint">放回后保持静止，不会自动播放。</p></div>
</section><aside><h2>模拟状态</h2><p id="connection" role="status">正在连接本地设备…</p>
<div class="actions"><button data-action="fault">模拟音频故障</button><button data-action="loading">模拟加载</button>
<button data-action="recover">恢复到静止</button><button data-action="empty">模拟电量耗尽</button></div>
<p class="hint">恢复不会清除节奏，也不会自动播放。</p><hr><h2>本次节奏</h2>
<p>在设备的设置页选择“导出本次节奏”，即可在这里下载 JSON。</p>
<a id="download" href="/session.json" download="echoy-arrange-B-session.json" hidden>下载本次节奏 JSON</a>
<p id="message" role="status"></p><hr><p class="hint">当前为单台 PC 模拟：音频合成和多设备联动尚未实现。</p>
</aside></div></main></body></html>""".encode()

STYLE = """*{box-sizing:border-box}body{margin:0;background:#eee9df;color:#38352f;font:16px/1.6 system-ui,sans-serif}
main{max-width:1200px;margin:auto;padding:32px 24px}header{margin-bottom:24px}h1{font-size:28px;margin:4px 0 8px}h2{font-size:18px;margin:0 0 12px}
p{margin:8px 0 16px}.eyebrow{font-size:12px;letter-spacing:.12em;color:#766d60}.desk{display:grid;grid-template-columns:minmax(0,1fr) 280px;gap:24px;align-items:start}
.device,aside{background:#fbf8f1;border:1px solid #d8d0c3;border-radius:12px;overflow:hidden}aside{padding:24px}
iframe{display:block;width:100%;height:790px;border:0;background:white}#shelf{min-height:480px;padding:100px 32px;text-align:center}
.actions{display:grid;gap:10px}button,a{font:inherit}button{padding:10px 14px;background:#f3ede2;color:inherit;border:1px solid #bfb4a2;border-radius:8px;cursor:pointer}
button:hover{background:#eae0cf}button:disabled{cursor:default;opacity:.45}button:focus-visible,a:focus-visible{outline:3px solid #a76036;outline-offset:3px}
#shelf button{background:#ae4e35;color:#fff;border-color:#ae4e35}a{color:#954127}.hint{font-size:13px;color:#756b5d}#message{font-size:14px}hr{border:0;border-top:1px solid #e2dace;margin:24px 0}
[hidden]{display:none!important}@media(max-width:850px){main{padding:20px 12px}.desk{grid-template-columns:1fr}aside{order:-1}.actions{grid-template-columns:1fr 1fr}iframe{height:760px}}""".encode()

SCRIPT = """'use strict';
const preview=document.querySelector('#preview'),shelf=document.querySelector('#shelf');
const connection=document.querySelector('#connection'),message=document.querySelector('#message'),download=document.querySelector('#download');
let previousCounter=null;
async function status(){
 try{
  const response=await fetch('/status',{cache:'no-store'});if(!response.ok)throw new Error('status');
  const state=await response.json();preview.hidden=state.removed;shelf.hidden=!state.removed;
  connection.textContent=state.connected?'设备已连接':'设备未连接，请先启动官方 GSP 预览和 Echoy 后端。';
  for(const button of document.querySelectorAll('[data-action]'))button.disabled=!state.connected;
  download.hidden=!state.export_available;
  if(state.export_available&&previousCounter!==null&&state.export_counter!==previousCounter)message.textContent='节奏已导出，可以下载。';
  previousCounter=state.export_counter;
 }catch(error){connection.textContent='本地控制服务暂时不可用。';for(const button of document.querySelectorAll('[data-action]'))button.disabled=true;}
}
for(const button of document.querySelectorAll('[data-action]'))button.addEventListener('click',async()=>{
 button.disabled=true;
 try{const response=await fetch('/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:button.dataset.action})});
  if(!response.ok)throw new Error('command');message.textContent='操作已发送。';
 }catch(error){message.textContent='操作未发送，请检查设备连接。';}
 await status();
});
async function refresh(){await status();setTimeout(refresh,600);}refresh();
""".encode()


class ControlDirectory:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
        details = os.fstat(self.fd)
        if details.st_uid != os.getuid() or stat.S_IMODE(details.st_mode) != 0o700:
            os.close(self.fd)
            raise PermissionError("control directory must be owned by this user and have mode 0700")
        self.socket_path = f"/proc/self/fd/{self.fd}/command.sock"

    def close(self):
        os.close(self.fd)

    def read_json(self, name):
        # Callers supply only the two fixed names below. NOFOLLOW also prevents
        # a local symlink from turning the download route into a file reader.
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=self.fd)
        try:
            details = os.fstat(fd)
            if not stat.S_ISREG(details.st_mode) or details.st_uid != os.getuid() or details.st_mode & 0o077 or details.st_size > MAX_JSON:
                raise ValueError("invalid control file")
            with os.fdopen(fd, "rb", closefd=False) as file:
                payload = file.read(MAX_JSON + 1)
            if len(payload) > MAX_JSON:
                raise ValueError("oversized control file")
            return json.loads(payload), payload
        finally:
            os.close(fd)

    def transport(self):
        details = os.stat("command.sock", dir_fd=self.fd, follow_symlinks=False)
        if not stat.S_ISSOCK(details.st_mode) or details.st_uid != os.getuid() or details.st_mode & 0o077:
            raise ValueError("invalid command socket")
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        connection.settimeout(.2)
        try:
            connection.connect(self.socket_path)
        except BaseException:
            connection.close()
            raise
        return connection

    def status(self):
        result = {"removed": False, "export_counter": 0, "export_available": False}
        try:
            saved, _ = self.read_json("status.json")
            if (type(saved.get("removed")) is not bool or type(saved.get("export_available")) is not bool
                    or type(saved.get("export_counter")) is not int or saved["export_counter"] < 0):
                raise ValueError("invalid status")
            result.update({key: saved[key] for key in result})
        except (OSError, ValueError, AttributeError):
            pass
        try:
            with self.transport():
                result["connected"] = True
        except (OSError, ValueError):
            result["connected"] = False
        return result

    def send(self, action):
        if action not in ACTIONS:
            raise ValueError("unknown action")
        with self.transport() as connection:
            connection.send(action.encode("ascii"))

    def session(self):
        if not self.status()["export_available"]:
            raise FileNotFoundError("no exported session")
        value, payload = self.read_json("session.json")
        if not isinstance(value, dict) or value.get("simulated") is not True or not isinstance(value.get("tiles"), dict):
            raise ValueError("invalid session")
        return payload


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(2)

    def log_message(self, format_string, *args):
        # The status poll is intentionally quiet; startup and fatal errors are
        # enough to identify this helper without flooding the simulator logs.
        pass

    def reply(self, status, payload=b"", content_type="application/json; charset=utf-8", attachment=False):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; frame-src http://127.0.0.1:3222; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        if attachment:
            self.send_header("Content-Disposition", 'attachment; filename="echoy-arrange-B-session.json"')
        self.end_headers()
        self.wfile.write(payload)

    def allowed(self, require_origin=False):
        host = f"127.0.0.1:{self.server.server_port}"
        origin = self.headers.get("Origin")
        if (self.headers.get("Host") != host or (require_origin and origin is None)
                or (origin is not None and origin != f"http://{host}")
                or self.headers.get("Sec-Fetch-Site", "none") not in {"none", "same-origin"}):
            self.close_connection = True
            self.reply(403, b'{"error":"same-origin loopback access required"}')
            return False
        return True

    def do_GET(self):
        if not self.allowed():
            return
        if self.path == "/":
            self.reply(200, PAGE, "text/html; charset=utf-8")
        elif self.path == "/style.css":
            self.reply(200, STYLE, "text/css; charset=utf-8")
        elif self.path == "/app.js":
            self.reply(200, SCRIPT, "text/javascript; charset=utf-8")
        elif self.path == "/status":
            self.reply(200, json.dumps(self.server.controls.status()).encode())
        elif self.path == "/session.json":
            try:
                payload = self.server.controls.session()
            except (OSError, ValueError):
                self.reply(404, b'{"error":"no valid exported session"}')
            else:
                self.reply(200, payload, attachment=True)
        else:
            self.reply(404, b'{"error":"unknown path"}')

    def do_POST(self):
        if not self.allowed(require_origin=True):
            return
        self.close_connection = True
        if self.path != "/command":
            self.reply(404, b'{"error":"unknown path"}')
            return
        raw_length = self.headers.get("Content-Length", "")
        if not raw_length.isdecimal() or self.headers.get("Transfer-Encoding"):
            self.reply(400, b'{"error":"fixed content length required"}')
            return
        if len(raw_length) > 4 or int(raw_length) > 128:
            self.reply(413, b'{"error":"command too large"}')
            return
        if self.headers.get("Content-Type", "").split(";", 1)[0].strip() != "application/json":
            self.reply(415, b'{"error":"JSON required"}')
            return
        try:
            body = json.loads(self.rfile.read(int(raw_length)))
            if not isinstance(body, dict) or set(body) != {"action"} or not isinstance(body["action"], str) or body["action"] not in ACTIONS:
                raise ValueError("invalid action")
        except (ValueError, OSError):
            self.reply(400, b'{"error":"unknown command"}')
            return
        try:
            self.server.controls.send(body["action"])
        except (OSError, ValueError):
            self.reply(503, b'{"error":"native preview unavailable"}')
        else:
            self.reply(202, b'{"accepted":true}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control-dir", default=os.environ.get("ECHOY_SIM_CONTROL_DIR"),
                        help="existing private 0700 directory shared with the native backend")
    parser.add_argument("--port", type=int, default=18895, help="loopback HTTP port (default: 18895)")
    args = parser.parse_args()
    if not args.control_dir or not 0 <= args.port <= 65535:
        parser.error("--control-dir is required and --port must be between 0 and 65535")
    controls = ControlDirectory(args.control_dir)
    try:
        with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
            server.controls = controls
            print(f"ECHOY_CONTROLS_READY http://127.0.0.1:{server.server_port}", flush=True)
            server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        controls.close()


if __name__ == "__main__":
    main()
