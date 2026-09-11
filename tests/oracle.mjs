#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const expectedHash = 'd23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5';
const referencePath = path.join(root, 'reference', 'Echoy_v5.0.3.html');
const manifestPath = path.join(root, 'reference', 'manifest.json');
const casesDir = path.join(root, 'tests', 'cases');
const musicPath = path.join(casesDir, 'music-fixtures.json');
const sessionPath = path.join(casesDir, 'session-trajectories.json');
const uiCasesPath = path.join(casesDir, 'ui-cases.json');
const uiCapturePath = path.join(casesDir, 'ui-capture.json');
const uiManifestPath = path.join(root, 'reference', 'ui', 'manifest.json');

const roles = ['pulse', 'voice', 'dust', 'harmony'];
const defaults = { pulse: .58, voice: .56, dust: .48, harmony: .50 };
const thresholdSamples = [.25, .26, .30, .42, .52, .65, .70, .72, .74, .75, .76, .78, .80, .82];
const bars = [0, 1, 2, 3, 4];
const duplicates = [0, 1, 2];
const eventFields = ['step', 'kind', 'vel', 'note', 'dur', 'accent', 'expression', 'notes', 'harmony', 'chord', 'articulation', 'maxDuration'];

const sourceMotifs = [
  { id: 'solo', source: null },
  { id: 'empty', source: { id: 'L', motif: [] } },
  { id: 'all-weak', source: { id: 'L', motif: [
    { step: 0, kind: 'tone', vel: .05, note: 62, dur: .08, accent: false },
    { step: 5, kind: 'grain', vel: .08, note: 69, dur: .12, accent: false },
    { step: 13, kind: 'tone', vel: .06, note: 65, dur: .10, accent: false },
  ] } },
  { id: 'sustained', source: { id: 'L', motif: [
    { step: 2, kind: 'tone', vel: .72, note: 65, dur: 1.24, accent: true },
    { step: 14, kind: 'tone', vel: .44, note: 69, dur: .86, accent: false },
  ] } },
  { id: 'chord', source: { id: 'L', motif: [
    { step: 1, kind: 'chord', vel: .68, note: 65, notes: [50, 57, 65], harmony: [50, 57, 65], dur: .58, accent: true },
    { step: 9, kind: 'chord', vel: .43, note: 69, notes: [53, 60, 69], harmony: [53, 60, 69], dur: .31, accent: false },
  ] } },
  { id: 'same-onset-polyphony', source: { id: 'L', motif: [
    { step: 4, kind: 'tone', vel: .62, note: 62, dur: .42, accent: true },
    { step: 4, kind: 'grain', vel: .37, note: 69, dur: .18, accent: false },
    { step: 4, kind: 'chord', vel: .51, note: 65, notes: [50, 57, 65], harmony: [50, 57, 65], dur: .72, accent: false },
    { step: 12, kind: 'tone', vel: .48, note: 67, dur: .26, accent: false },
  ] } },
];

function hash(data) {
  return crypto.createHash('sha256').update(data).digest('hex');
}

function stableJson(value) {
  return JSON.stringify(value, null, 2) + '\n';
}

function fixtureJson(value) {
  return JSON.stringify(value) + '\n';
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function writeStable(file, data) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  if (!fs.existsSync(file) || fs.readFileSync(file, 'utf8') !== data) fs.writeFileSync(file, data);
}

function loadOracle() {
  const html = fs.readFileSync(referencePath, 'utf8');
  assert.equal(hash(html), expectedHash, 'reference SHA-256 changed');
  const beginMarker = '/* Complete-phrase response';
  const endMarker = '/* Simulator only:';
  const begin = html.indexOf(beginMarker);
  const end = html.indexOf(endMarker, begin + beginMarker.length);
  assert(begin >= 0 && end > begin, 'product rule module boundaries missing');
  assert.equal(html.indexOf(beginMarker, begin + beginMarker.length), -1, 'duplicate product-rule start marker');
  assert.equal(html.indexOf(endMarker, end + endMarker.length), -1, 'duplicate product-rule end marker');
  const sandbox = vm.createContext({ console });
  vm.runInContext(html.slice(begin, end), sandbox, { filename: referencePath });
  return sandbox;
}

function encodeEvent(event) {
  return eventFields.map(field => event[field] ?? null);
}

function generateMusic(sandbox) {
  const engine = new sandbox.DuijuEngine();
  const cases = [];
  let eventCount = 0;
  for (const role of roles) {
    const activities = [0, ...thresholdSamples, defaults[role], 1].sort((a, b) => a - b);
    assert.equal(new Set(activities).size, 17, `${role} activity samples must be unique`);
    for (let phrase = 1; phrase <= 3; phrase++) {
      for (const activity of activities) {
        for (const bar of bars) {
          for (let sourceIndex = 0; sourceIndex < sourceMotifs.length; sourceIndex++) {
            for (const duplicate of duplicates) {
              const device = { role, phrase, committedActivity: activity };
              const output = engine.phrase(device, sourceMotifs[sourceIndex].source, bar, duplicate);
              assert(output.length <= 16, 'prototype event budget exceeded');
              for (const event of output) {
                assert(Number.isInteger(event.step) && event.step >= 0 && event.step < 16, 'event step outside one bar');
                assert(Number.isFinite(event.dur) && event.dur > 0, 'event duration must be positive');
                assert(Number.isFinite(event.vel) && event.vel >= .05 && event.vel <= 1, 'event velocity outside contract');
              }
              eventCount += output.length;
              cases.push([role, phrase, activity, bar, sourceIndex, duplicate, output.map(encodeEvent)]);
            }
          }
        }
      }
    }
  }
  return {
    schemaVersion: 1,
    oracleSha256: expectedHash,
    bpm: 96,
    dimensions: {
      roles,
      phrases: [1, 2, 3],
      activitySamples: { thresholds: thresholdSamples, defaults, endpoints: [0, 1] },
      bars,
      leftMotifs: sourceMotifs.map(({ id, source }) => ({ id, motif: source?.motif ?? null })),
      duplicates,
    },
    tupleFields: ['role', 'phrase', 'committedActivity', 'bar', 'leftMotifIndex', 'duplicate', 'events'],
    eventFields,
    caseCount: cases.length,
    eventCount,
    cases,
  };
}

function planSummary(session, at = session.time) {
  return JSON.parse(JSON.stringify(session.composeAt(at).map(entry => ({
    id: entry.id,
    role: entry.role,
    source: entry.source,
    transport: session.devices[entry.id].transport,
    motifAvailable: session.devices[entry.id].motifAvailable,
    eventCount: entry.events.length,
    motifCount: entry.motif.length,
    motif: entry.motif.map(encodeEvent),
  }))));
}

function checkpoint(session, label, details = {}) {
  return { label, at: session.time, details, snapshot: session.snapshot(), plan: planSummary(session) };
}

function readyPair(Session, start = 10) {
  const session = new Session(start);
  session.tick(start + .71);
  session.tick(start + .72);
  assert.equal(session.devices.A.lifecycle, 'ready');
  assert.equal(session.devices.B.lifecycle, 'ready');
  return session;
}

function joinedPair(Session, start = 20) {
  const session = readyPair(Session, start);
  session.receiveContacts([{ left: 'A', right: 'B', state: 'connected' }], session.time);
  session.tick(session.time + .46);
  const merge = Object.values(session.merges)[0];
  assert(merge, 'stable contact must start synchronization');
  session.tick(merge.readyAt + .001);
  assert.equal(merge.stage, 'waiting');
  session.tick(merge.applyAt + .001);
  assert.equal(session.devices.A.sessionId, session.devices.B.sessionId);
  assert.equal(session.committedEdges['A>B'], true);
  return session;
}

function generateTrajectories(sandbox) {
  const Session = sandbox.DuijuSession;
  const trajectories = [];

  {
    const session = new Session(10);
    const steps = [checkpoint(session, 'powered-on-with-independent-clocks')];
    session.tick(10.71);
    session.tick(10.72);
    steps.push(checkpoint(session, 'discovered-but-not-joined'));
    assert.notEqual(session.devices.A.sessionId, session.devices.B.sessionId);
    assert.equal(JSON.stringify(session.devices.A.discovered), '["B"]');
    trajectories.push({ id: 'discovery-does-not-join', steps });
  }

  {
    const session = readyPair(Session, 20);
    const steps = [checkpoint(session, 'ready-independent')];
    session.receiveContacts([{ left: 'A', right: 'B', state: 'connected' }], session.time);
    steps.push(checkpoint(session, 'contact-seen'));
    session.tick(session.time + .46);
    steps.push(checkpoint(session, 'synchronizing'));
    const merge = Object.values(session.merges)[0];
    session.tick(merge.readyAt + .001);
    steps.push(checkpoint(session, 'beat-agreed-waiting-for-bar', { applyAt: merge.applyAt }));
    session.tick(merge.applyAt + .001);
    steps.push(checkpoint(session, 'joined-at-future-bar'));
    assert.equal(session.devices.A.transport, 'paused');
    assert.equal(session.devices.B.transport, 'paused');
    trajectories.push({ id: 'contact-joins-without-autoplay', steps });
  }

  {
    const session = joinedPair(Session, 30);
    const steps = [checkpoint(session, 'joined-paused')];
    assert.equal(session.requestPlay('B'), true);
    const playAt = session.devices.B.playAt;
    steps.push(checkpoint(session, 'play-request-waits-for-beat', { playAt }));
    session.tick(playAt - .001);
    steps.push(checkpoint(session, 'still-waiting-before-beat'));
    session.tick(playAt + .001);
    steps.push(checkpoint(session, 'playing-on-beat'));
    assert.equal(session.devices.B.transport, 'playing');
    trajectories.push({ id: 'local-play-starts-on-next-beat', steps });
  }

  {
    const session = joinedPair(Session, 40);
    const sharedSession = session.devices.A.sessionId;
    const steps = [checkpoint(session, 'connected-response-active')];
    session.receiveContacts([], session.time);
    steps.push(checkpoint(session, 'released-edge-still-committed'));
    session.updateArrangements();
    const arrangement = session.arrangements[sharedSession];
    assert(arrangement, 'release must queue a boundary arrangement');
    session.tick(arrangement.applyAt + .001);
    steps.push(checkpoint(session, 'response-removed-on-bar-shared-clock-kept'));
    assert.equal(session.devices.A.sessionId, sharedSession);
    assert.equal(session.devices.B.sessionId, sharedSession);
    assert.equal(session.committedEdges['A>B'], undefined);
    trajectories.push({ id: 'release-removes-response-not-shared-clock', steps });
  }

  {
    const session = joinedPair(Session, 50);
    for (const id of ['A', 'B']) {
      session.devices[id].transport = 'playing';
      session.devices[id].muted = false;
    }
    const sourceMotif = planSummary(session).find(x => x.id === 'A').motif;
    const steps = [checkpoint(session, 'playing-with-valid-source')];
    session.devices.A.volume = 0;
    steps.push(checkpoint(session, 'volume-zero-keeps-motif'));
    assert.deepEqual(planSummary(session).find(x => x.id === 'A').motif, sourceMotif);
    session.devices.A.fault = 'audio';
    steps.push(checkpoint(session, 'output-fault-mutes-events-keeps-motif'));
    const audioFaultPlan = planSummary(session).find(x => x.id === 'A');
    assert.equal(audioFaultPlan.eventCount, 0);
    assert.deepEqual(audioFaultPlan.motif, sourceMotif);
    session.devices.A.fault = 'data';
    const active = session.sessionFor('A');
    const nextBar = session.nextBoundary(active, session.time + .01, 16);
    session.tick(nextBar + .001);
    steps.push(checkpoint(session, 'data-fault-removes-motif-on-boundary'));
    const dataPlan = planSummary(session);
    assert.equal(dataPlan.find(x => x.id === 'A').motifCount, 0);
    assert.equal(dataPlan.find(x => x.id === 'B').source, null);
    trajectories.push({ id: 'motif-availability-is-separate-from-output', steps });
  }

  {
    const session = readyPair(Session, 60);
    const steps = [checkpoint(session, 'pulse-default-memory')];
    assert.equal(session.queueRole('A', 'harmony'), true);
    assert.equal(session.queuePhrase('A', 3), true);
    session.commitIdle('A');
    steps.push(checkpoint(session, 'idle-role-commit-restores-harmony-memory'));
    session.setActivity('A', .75);
    session.commitIdle('A');
    steps.push(checkpoint(session, 'idle-activity-commit'));
    assert.equal(session.devices.A.role, 'harmony');
    assert.equal(session.devices.A.phrase, 3);
    assert.equal(session.devices.A.committedActivity, .75);
    trajectories.push({ id: 'idle-parameter-commit', steps });
  }

  return {
    schemaVersion: 1,
    oracleSha256: expectedHash,
    trajectoryCount: trajectories.length,
    checkpointCount: trajectories.reduce((count, trajectory) => count + trajectory.steps.length, 0),
    trajectories,
  };
}

function generateUiCases() {
  const states = [
    ...roles.map(role => ({ id: `perform-${role}`, route: 'perform', role, phrase: role === 'harmony' ? 2 : 1, activity: defaults[role], state: 'paused' })),
    { id: 'perform-playing', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing' },
    { id: 'perform-waiting', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'waiting' },
    { id: 'perform-pending-activity', route: 'perform', role: 'dust', phrase: 1, activity: .78, committedActivity: .48, state: 'paused' },
    ...roles.map(role => ({ id: `role-${role}`, route: 'role', role, phrase: role === 'harmony' ? 2 : 1, activity: defaults[role], state: 'paused' })),
    { id: 'volume', route: 'perform', role: 'pulse', phrase: 1, activity: .58, volume: .70, state: 'volume' },
    { id: 'boot', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'booting' },
    { id: 'loading-data', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'loading' },
    { id: 'loading-role', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'role-preparing', preparingRole: 'harmony' },
    { id: 'error-role', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'role-error' },
    { id: 'error-data', route: 'perform', role: 'dust', phrase: 1, activity: .48, state: 'data-error' },
    { id: 'error-audio', route: 'perform', role: 'harmony', phrase: 2, activity: .50, state: 'audio-error' },
    { id: 'low-battery', route: 'perform', role: 'pulse', phrase: 1, activity: .58, battery: 12, state: 'paused' },
  ];
  const cases = ['zh-CN', 'en'].flatMap(locale => states.map(state => ({ locale, ...state })));
  return {
    schemaVersion: 1,
    oracleSha256: expectedHash,
    viewport: { width: 480, height: 480, deviceScaleFactor: 1 },
    fixedTimeMs: 10000,
    animation: 'disabled',
    fontFaces: [
      { family: 'Echoy Latin', style: 'normal', weight: 400, source: '/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf' },
      { family: 'Echoy Latin', style: 'normal', weight: 700, source: '/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf' },
      { family: 'Echoy CJK', style: 'normal', weight: 400, source: '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc' },
      { family: 'Echoy CJK', style: 'normal', weight: 700, source: '/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc' },
      { family: 'Echoy Mono', style: 'normal', weight: 400, source: '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf' },
      { family: 'Echoy Mono', style: 'normal', weight: 700, source: '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf' },
    ],
    caseCount: cases.length,
    cases,
  };
}

function generateAll() {
  const sandbox = loadOracle();
  return { music: generateMusic(sandbox), sessions: generateTrajectories(sandbox), uiCases: generateUiCases() };
}

function verifyGenerated(actualPath, expected, label, serialize = stableJson) {
  assert(fs.existsSync(actualPath), `missing generated ${label}: ${path.relative(root, actualPath)}`);
  const actual = fs.readFileSync(actualPath, 'utf8');
  assert.equal(actual, serialize(expected), `${label} is stale; run node tests/oracle.mjs --generate`);
}

function verifyUiCapture(uiCases) {
  assert(fs.existsSync(uiCapturePath), 'missing UI capture; run node tools/capture_reference.mjs --generate');
  assert(fs.existsSync(uiManifestPath), 'missing UI manifest; run node tools/capture_reference.mjs --generate');
  const capture = readJson(uiCapturePath);
  const manifest = readJson(uiManifestPath);
  assert.equal(capture.oracleSha256, expectedHash);
  assert.equal(manifest.oracleSha256, expectedHash);
  assert.equal(capture.caseCount, uiCases.caseCount);
  assert.equal(manifest.caseCount, uiCases.caseCount);
  assert.equal(capture.cases.length, uiCases.caseCount);
  assert.equal(manifest.images.length, uiCases.caseCount);
  assert.equal(capture.browser.version, manifest.browser.version);
  const expectedIds = uiCases.cases.map(item => `${item.locale}/${item.id}`);
  assert.deepEqual(capture.cases.map(item => item.id), expectedIds);
  assert.deepEqual(manifest.images.map(item => item.id), expectedIds);
  for (const image of manifest.images) {
    const file = path.join(root, image.path);
    assert(fs.existsSync(file), `missing UI baseline: ${image.path}`);
    const bytes = fs.readFileSync(file);
    assert.equal(bytes.readUInt32BE(16), 480, `${image.path} width changed`);
    assert.equal(bytes.readUInt32BE(20), 480, `${image.path} height changed`);
    assert.equal(hash(bytes), image.sha256, `${image.path} SHA-256 changed`);
  }
  for (const font of capture.fonts) {
    assert(fs.existsSync(font.source), `frozen font is unavailable: ${font.source}`);
    assert.equal(hash(fs.readFileSync(font.source)), font.sha256, `frozen font changed: ${font.source}`);
  }
  for (const item of capture.cases) {
    assert.deepEqual(item.canvas.motifSteps, item.motif.map(event => event.step), `${item.id} score does not describe the captured motif`);
    assert.equal(item.screen.width, 480);
    assert.equal(item.screen.height, 480);
  }
}

function selfTest() {
  const manifest = readJson(manifestPath);
  assert.equal(manifest.sha256, expectedHash);
  assert.equal(manifest.embeddedVersion, '5.0.4');
  assert.deepEqual(Object.keys(manifest.modules), ['response', 'harmony', 'engine', 'session']);
  const html = fs.readFileSync(referencePath, 'utf8');
  for (const [name, module] of Object.entries(manifest.modules)) {
    assert.equal(typeof module.path, 'string', `${name} manifest entry lacks extraction metadata`);
    const modulePath = path.join(root, module.path);
    const moduleBytes = fs.readFileSync(modulePath);
    assert.equal(hash(moduleBytes), module.sha256, `${name} extracted module changed`);
    assert.equal(moduleBytes.length, module.size, `${name} extracted module size changed`);
    assert.equal(html.split(module.beginMarker).length - 1, 1, `${name} begin marker is not unique`);
    assert.equal(html.split(module.endMarker).length - 1, 1, `${name} end marker is not unique`);
    const begin = html.indexOf(module.beginMarker);
    const end = html.indexOf(module.endMarker, begin + module.beginMarker.length);
    assert.equal(moduleBytes.toString('utf8'), html.slice(begin, end).trimEnd() + '\n', `${name} is not the source slice`);
  }
  const sandbox = loadOracle();
  const engine = new sandbox.DuijuEngine();
  const notes = engine.phrase(engine.devices.A, null, 0, 0);
  assert(notes.some(note => note.kind === 'kick' && note.step === 0));
  const session = new sandbox.DuijuSession(0);
  session.tick(1);
  assert.notEqual(session.devices.A.sessionId, session.devices.B.sessionId);

  const generated = generateAll();
  verifyGenerated(musicPath, generated.music, 'music fixtures', fixtureJson);
  verifyGenerated(sessionPath, generated.sessions, 'session trajectories');
  verifyGenerated(uiCasesPath, generated.uiCases, 'UI cases');
  verifyUiCapture(generated.uiCases);

  const first = fixtureJson(generated.music) + stableJson(generated.sessions) + stableJson(generated.uiCases);
  const secondGenerated = generateAll();
  const second = fixtureJson(secondGenerated.music) + stableJson(secondGenerated.sessions) + stableJson(secondGenerated.uiCases);
  assert.equal(hash(first), hash(second), 'two in-memory generations differ');
  process.stdout.write(stableJson({
    ok: true,
    sha256: expectedHash,
    fixtureCases: generated.music.caseCount,
    fixtureEvents: generated.music.eventCount,
    trajectories: generated.sessions.trajectoryCount,
    checkpoints: generated.sessions.checkpointCount,
    uiCases: generated.uiCases.caseCount,
    deterministicSha256: hash(first),
  }));
}

if (process.argv.includes('--generate')) {
  const generated = generateAll();
  writeStable(musicPath, fixtureJson(generated.music));
  writeStable(sessionPath, stableJson(generated.sessions));
  writeStable(uiCasesPath, stableJson(generated.uiCases));
  process.stdout.write(stableJson({
    ok: true,
    fixtureCases: generated.music.caseCount,
    fixtureEvents: generated.music.eventCount,
    trajectories: generated.sessions.trajectoryCount,
    checkpoints: generated.sessions.checkpointCount,
    uiCases: generated.uiCases.caseCount,
  }));
} else if (process.argv.includes('--self-test')) {
  selfTest();
} else {
  process.stderr.write('usage: node tests/oracle.mjs --generate | --self-test\n');
  process.exitCode = 2;
}
