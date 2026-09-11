#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Loopback-only desktop reference and same-C-firmware development simulator."""
import argparse
import base64
from collections import deque
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import selectors
import struct
import subprocess
import tempfile
import time
import wave
import zlib

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
REFERENCE = ROOT / 'reference/Echoy_v5.0.3.html'
HASH = 'd23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5'
IDS = 'ABC'
MAX_REQUEST = 8192
MAX_RESPONSE = 131072


def pinned():
    raw = REFERENCE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != HASH:
        raise RuntimeError('Reference hash mismatch; refusing to serve modified reference')
    return raw


def section(source, start, end):
    if source.count(start) != 1 or source.count(end) != 1:
        raise RuntimeError('Ambiguous reference module boundary')
    return source.split(start, 1)[1].split(end, 1)[0]


class Assets:
    """Explicit in-memory allowlist. Never resolve a client-supplied path."""
    def __init__(self, mode):
        raw = pinned()
        self.paths = {'/': (raw, 'text/html; charset=utf-8')}
        if mode == 'firmware':
            source = raw.decode()
            prefix = source.split('<script>', 1)[0]
            i18n = '/* 合拍 / Echoy: presentation-only localization.' + section(source, '/* 合拍 / Echoy: presentation-only localization.', '/* Complete-phrase response rules,')
            roles = 'window.DUIJU_ROLES=Object.freeze({' + section(source, 'const ROLES=Object.freeze({', 'const clamp=(v,a=0,b=1)')
            roles = roles.rstrip()  # Contains the original terminating }); only.
            spatial = '/* Simulator only:' + section(source, '/* Simulator only:', '/* Browser sound adapter only.')
            desk = '/* Direct manipulation adapter.' + section(source, '/* Direct manipulation adapter.', '/* Host owns browser adapters')
            guide = '/* HTML-only, optional onboarding.' + section(source, '/* HTML-only, optional onboarding.', '/* One simulator language preference,')
            page = prefix + '<script>' + i18n + roles + spatial + desk + '</script><script src="/firmware.js"></script><script>' + guide + '</script><script>window.echoy=window.duiju;window.echoyGuide=window.duijuGuide;window.duiju.start();</script></body></html>'
            self.paths['/'] = (page.encode(), 'text/html; charset=utf-8')
            for name, kind in [('firmware.js', 'text/javascript'), ('pcm-worklet.js', 'text/javascript')]:
                self.paths['/' + name] = ((HERE / name).read_bytes(), kind)

    def get(self, path):
        return self.paths.get(path)


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key')
        result[key] = value
    return result


def parse_request(raw):
    if not 0 < len(raw) <= MAX_REQUEST:
        raise ValueError('Request length out of bounds')
    try:
        request = json.loads(raw, object_pairs_hook=no_duplicates,
                             parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))
    except (UnicodeError, RecursionError, json.JSONDecodeError) as exc:
        raise ValueError('Invalid JSON') from exc
    schemas = {'snapshot': set(), 'tick': {'ms'}, 'touch': {'id', 'phase', 'x', 'y'},
               'contacts': {'edges', 'uncertain'}, 'power': {'id', 'on'},
               'fault': {'id', 'name', 'value'}, 'ui': {'id', 'action', 'value'},
               'locale': {'english'}, 'reset': set()}
    if not isinstance(request, dict) or not isinstance(request.get('op'), str) or request.get('op') not in schemas:
        raise ValueError('Unknown operation')
    op = request['op']
    if set(request) != schemas[op] | {'op'}:
        raise ValueError('Unexpected or missing fields')
    def number(key, low, high, integer=False):
        value = request[key]
        if type(value) not in (int, float) or not low <= value <= high or (integer and int(value) != value):
            raise ValueError('Invalid ' + key)
    if 'id' in request and request['id'] not in list(IDS):
        raise ValueError('Unknown device')
    if op == 'tick': number('ms', 0, 40000, True)
    if op == 'touch':
        number('phase', 0, 3, True); number('x', -480, 960, True); number('y', -480, 960, True)
    if op == 'ui':
        number('action', 0, 12, True); number('value', 0, 3)
    if op == 'power' and type(request['on']) is not bool: raise ValueError('Invalid power')
    if op == 'locale' and type(request['english']) is not bool: raise ValueError('Invalid locale')
    if op == 'fault':
        if request['name'] not in ('loading', 'role-fail', 'audio-fail', 'data-fail', 'sync-fail', 'battery', 'restore', 'moving', 'wifi-off', 'peer-off'):
            raise ValueError('Unknown fault')
        number('value', 0, 100 if request['name'] == 'battery' else 1)
    if op == 'contacts':
        edges, uncertain = request['edges'], request['uncertain']
        if not isinstance(edges, list) or len(edges) > 2 or not isinstance(uncertain, list) or len(uncertain) > 3 or any(i not in list(IDS) for i in uncertain):
            raise ValueError('Invalid contacts')
        for edge in edges:
            if not isinstance(edge, list) or len(edge) != 2 or any(i not in list(IDS) for i in edge) or edge[0] == edge[1]:
                raise ValueError('Invalid edge')
        if len({e[0] for e in edges}) != len(edges) or len({e[1] for e in edges}) != len(edges) or (len(edges) == 2 and edges[0] == edges[1][::-1]):
            raise ValueError('Contacts must form a directed chain')
    return request


def png(ppm):
    header = b'P6\n480 480\n255\n'
    if not ppm.startswith(header) or len(ppm) != len(header) + 480*480*3:
        raise RuntimeError('Invalid framebuffer')
    rgb = ppm[len(header):]
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    scanlines = b''.join(b'\0' + rgb[y*1440:(y+1)*1440] for y in range(480))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 480, 480, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scanlines, 1)) + chunk(b'IEND', b'')


class Runner:
    def __init__(self, index, directory):
        self.frame = directory / (IDS[index] + '.ppm')
        self.process = subprocess.Popen([os.environ.get('ECHOY_SIM_RUNNER', str(ROOT/'build-host/echoy_sim_runner')), str(100+index), str(self.frame)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.pending = bytearray()

    def command(self, line):
        if len(line) > 510 or '\n' in line:
            raise ValueError('Invalid fixed command')
        self.process.stdin.write((line + '\n').encode('ascii'))
        deadline = time.monotonic() + 5
        while b'\n' not in self.pending:
            if not self.selector.select(max(0, deadline-time.monotonic())):
                raise RuntimeError('C runner response deadline exceeded')
            data = self.process.stdout.read(4096)
            if not data:
                raise RuntimeError('C runner disconnected')
            self.pending.extend(data)
            if len(self.pending) > MAX_RESPONSE:
                raise RuntimeError('C runner response exceeded bound')
        line, _, rest = self.pending.partition(b'\n')
        self.pending = bytearray(rest)
        return json.loads(line)

    def close(self):
        self.selector.close()
        self.process.stdin.close()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=2)
        self.process.stdout.close(); self.process.stderr.close()


class Bridge:
    """A single serialized owner for fake time, three C processes and packet I/O.

    This is deliberately a PC development bridge, with no hardware transport.
    Disconnect sends no pause/power/transport command. Without a request the
    fake clock and injections stop; the actual MCU runtime owns its own clock.
    """
    def __init__(self):
        (ROOT/'output/sim').mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(prefix='live-', dir=ROOT/'output/sim')
        self.runners = []
        self.time_us = 0
        self.states = [None]*3
        self.queue = deque()
        self.offline = [False]*3
        self.wifi = [True]*3
        self.edges, self.uncertain = [], []
        self.logs = deque(maxlen=100)
        self.sequence = 0
        try:
            for i in range(3):
                self.runners.append(Runner(i, Path(self.directory.name)))
                self.call(i, 'T 0')
            self.call(2, 'B 0')
        except Exception:
            self.close()
            raise

    def __enter__(self): return self
    def __exit__(self, *_): self.close()

    def close(self):
        for runner in self.runners: runner.close()
        self.directory.cleanup()

    def call(self, index, command):
        state = self.runners[index].command(command)
        for target, payload in state.pop('packets'):
            if len(self.queue) >= 4096: raise RuntimeError('Network queue capacity exceeded')
            self.queue.append((index, target, payload, state['boot']))
        self.states[index] = state
        return state

    def deliver(self):
        budget = 4096
        while self.queue:
            budget -= 1
            if budget < 0: raise RuntimeError('Network delivery budget exceeded')
            sender, target, payload, boot = self.queue.popleft()
            for to in range(3):
                if to != sender and (not target or target == 100+to) and not self.offline[sender] and not self.offline[to] and self.states[sender]['present'] and self.states[to]['present']:
                    self.call(to, f'R {100+sender} {boot} {payload}')

    def contacts(self):
        for i, name in enumerate(IDS):
            left = next((100+IDS.index(a) for a,b in self.edges if b == name and self.states[IDS.index(a)]['present']), 0)
            right = next((100+IDS.index(b) for a,b in self.edges if a == name and self.states[IDS.index(b)]['present']), 0)
            uncertain = name in self.uncertain or any(a in self.uncertain or b in self.uncertain for a,b in self.edges if name in (a,b))
            self.call(i, f'C {left} {right} {int(uncertain)}')

    def advance(self, ms):
        end = self.time_us + ms*1000
        while self.time_us < end:
            self.time_us = min(end, self.time_us+10000)
            for i in range(3): self.call(i, f'T {self.time_us}')
            self.deliver()

    def publish(self):
        devices = []
        for i in range(3):
            d = self.call(i, 'S').copy()
            d.update(id=IDS[i], frame_revision=d['revision'], audio_revision=d['revision'], audio_rate=48000,
                     frame='data:image/png;base64,'+base64.b64encode(png(self.runners[i].frame.read_bytes())).decode(),
                     pcm=base64.b64encode(bytes.fromhex(d['pcm'])).decode(), wifi=self.wifi[i], linkOnline=not self.offline[i])
            devices.append(d)
        self.sequence += 1
        return {'mode':'firmware-development', 'sequence':self.sequence, 'time_us':self.time_us, 'devices':devices,
                'contacts':self.edges, 'uncertain':self.uncertain, 'log':list(self.logs)}

    def request(self, request):
        r = parse_request(json.dumps(request, allow_nan=False).encode())
        op = r['op']; i = IDS.index(r['id']) if 'id' in r else 0
        if op == 'tick': self.advance(r['ms'])
        elif op == 'touch': self.call(i, f"P {r['phase']} {r['x']} {r['y']}")
        elif op == 'ui': self.call(i, f"U {r['action']} {r['value']}")
        elif op == 'power':
            self.call(i, 'B '+str(int(r['on']))); self.contacts()
        elif op == 'contacts':
            self.edges, self.uncertain = r['edges'], r['uncertain']; self.contacts()
        elif op == 'locale':
            for j in range(3): self.call(j, 'L '+str(int(r['english'])))
        elif op == 'fault':
            name, value = r['name'], r['value']
            if name == 'wifi-off': self.wifi[i] = not value
            elif name == 'peer-off': self.offline[i] = bool(value)
            else:
                fault = ['loading','role-fail','audio-fail','data-fail','sync-fail','battery','restore','moving'].index(name)
                self.call(i, f'F {fault} {value}')
                if name == 'restore':
                    self.offline[i] = False; self.wifi[i] = True
                    self.uncertain = [x for x in self.uncertain if x != IDS[i]]
                    self.contacts()
        elif op == 'reset':
            self.close(); self.__init__()
        self.deliver()
        if op not in ('snapshot', 'tick'):
            self.logs.append(f"{self.time_us/1e6:.3f}s {json.dumps(r, ensure_ascii=False)}")
        return self.publish()


def scenarios():
    checks = []
    def check(name, condition):
        if not condition: raise AssertionError(name)
        checks.append(name)
    with Bridge() as b:
        def request(op, **kw): return b.request(dict(op=op, **kw))
        def d(i=0): return b.states[i]
        def fault(name, value=1, id='A'): return request('fault', id=id, name=name, value=value)
        check('three instances / independent paused boot', all(s['transport']=='paused' for s in b.states))
        b.advance(1000)
        check('discovery does not join', all(s['members']==1 for s in b.states))
        request('ui',id='A',action=3,value=0);b.advance(750)
        check('local play starts',d()['transport']=='playing')
        intent=d()['transport']
        fault('loading');check('loading visible',d()['loading']);request('ui',id='A',action=12,value=0)
        check('loading cancelled',not d()['loading']);b.advance(3000);check('cancel does not fire late',not d()['loading'])
        fault('role-fail');old=d()['role'];request('ui',id='A',action=4,value=3);b.advance(700)
        check('role failure keeps previous role',d()['role']==old and d()['roleError'])
        fault('restore');check('restore keeps play intent',d()['transport']==intent and not d()['roleError'])
        fault('audio-fail');b.advance(3000);check('output fault keeps motif',d()['outputFault'] and d()['motifAvailable'])
        fault('restore');fault('data-fail');b.advance(3000)
        check('data failure invalidates motif',not d()['motifAvailable']);fault('restore');b.advance(3000)
        check('data recovery restores motif',d()['motifAvailable'])
        request('contacts',edges=[['A','B']],uncertain=['A']);b.advance(1000)
        check('uncertain does not authorize',d()['members']==1)
        fault('sync-fail');request('contacts',edges=[['A','B']],uncertain=[]);b.advance(7000)
        check('sync failure does not pretend success',d()['members']==1 and d()['syncFail'] and d()['feedback']=='sync-failed')
        fault('restore');b.advance(30000)
        check('real protocol joins pair',d()['members']==2 and d(1)['members']==2)
        check('join never auto plays peer',d(1)['transport']=='paused')
        fault('wifi-off');b.advance(3000);check('wifi off leaves peer music online',d()['members']==2 and not b.offline[0])
        request('contacts',edges=[],uncertain=[]);b.advance(3000)
        check('separation keeps shared time membership',d()['members']==2)
        fault('peer-off');b.advance(7000);check('peer loss splits remaining device',d(1)['members']==1)
        fault('restore');request('contacts',edges=[['A','B']],uncertain=[]);b.advance(30000)
        request('power',id='C',on=True);b.advance(400);check('third device boot remains paused',d(2)['present'] and d(2)['transport']=='paused')
        request('contacts',edges=[['A','B'],['B','C']],uncertain=[]);b.advance(35000)
        check('third joins via same protocol',all(s['members']==3 for s in b.states))
        before=d()['transport'];fault('audio-fail');edges=list(b.edges);fault('restore')
        check('fault restore preserves arrangement and intent',b.edges==edges and d()['transport']==before)
        fault('battery',10);check('low battery does not power off',d()['battery']==10 and d()['present'])
        fault('battery',0);check('zero battery powers off',not d()['present']);fault('battery',86)
        request('power',id='A',on=True);b.advance(400);check('reboot never auto plays',d()['transport']=='paused')
        request('power',id='B',on=False);request('power',id='C',on=False);check('single presence',sum(s['present'] for s in b.states)==1)
        request('power',id='A',on=False);check('all missing',not any(s['present'] for s in b.states))
        request('reset');check('reset restores initial presence', [s['present'] for s in b.states]==[True,True,False])
        request('ui',id='A',action=3,value=0)
        pcm = bytearray()
        for _ in range(75): pcm.extend(base64.b64decode(request('tick',ms=40)['devices'][0]['pcm']))
        check('C DSP produces nonzero PCM', any(pcm))
        wavpath=ROOT/'output/sim/firmware.wav'
        with wave.open(str(wavpath),'wb') as wav:
            wav.setparams((2,2,48000,0,'NONE','not compressed'));wav.writeframes(pcm)
        check('WAV exact duration',len(pcm)==48000*3*4)
        result={'passed':True,'checks':checks,'wav':str(wavpath.relative_to(ROOT))}
        (ROOT/'output/sim/scenarios.json').write_text(json.dumps(result,indent=2)+'\n')
        return result


class Server(HTTPServer):
    allow_reuse_address = True
    def __init__(self, port, mode):
        self.assets=Assets(mode);self.bridge=None
        super().__init__(('127.0.0.1',port), Handler)
        try:
            self.bridge=Bridge() if mode=='firmware' else None
        except Exception:
            super().server_close()
            raise
    def server_close(self):
        super().server_close()
        if self.bridge: self.bridge.close();self.bridge=None


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup();self.connection.settimeout(5)
    def log_message(self, *_): pass
    def respond(self, status, body=b'', kind='application/json'):
        self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff')
        self.end_headers();self.wfile.write(body)
    def local(self):
        expected='127.0.0.1:'+str(self.server.server_port)
        return self.headers.get('Host')==expected and self.headers.get('Origin', 'http://'+expected)=='http://'+expected
    def do_GET(self):
        if not self.local(): return self.respond(403)
        asset=self.server.assets.get(self.path)
        if asset is None:return self.respond(404)
        self.respond(200,*asset)
    def do_POST(self):
        if not self.local():return self.respond(403)
        if self.path!='/api' or not self.server.bridge:return self.respond(404)
        if self.headers.get('Content-Type')!='application/json' or self.headers.get('Transfer-Encoding'):return self.respond(415)
        try:
            length=int(self.headers.get('Content-Length','0'))
            if not 0<length<=MAX_REQUEST: raise ValueError('Invalid body length')
            request=parse_request(self.rfile.read(length))
            result=self.server.bridge.request(request)
            self.respond(200,json.dumps(result,separators=(',',':')).encode())
        except (ValueError, TimeoutError) as exc:self.respond(400,json.dumps({'error':str(exc)}).encode())
        except (BrokenPipeError,ConnectionResetError):pass
        except RuntimeError as exc:self.respond(503,json.dumps({'error':str(exc)}).encode())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('reference','firmware'),default='firmware')
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--headless',action='store_true')
    parser.add_argument('--scenario',choices=('all',),default='all')
    args=parser.parse_args()
    if args.mode=='firmware':
        # Reuse the repository's host build and its exact C libraries.
        subprocess.run(['cmake','-S',str(ROOT/'pc'),'-B',str(ROOT/'build-host')],check=True,stdout=subprocess.DEVNULL)
        subprocess.run(['cmake','--build',str(ROOT/'build-host'),'--target','echoy_sim_runner','-j8'],check=True,stdout=subprocess.DEVNULL)
    if args.headless:print(json.dumps(scenarios(),ensure_ascii=False,indent=2));return
    with Server(args.port,args.mode) as server:
        print(f'Echoy {args.mode} http://127.0.0.1:{server.server_port} (local development only)',flush=True)
        try:server.serve_forever()
        except KeyboardInterrupt:pass

if __name__=='__main__':main()
