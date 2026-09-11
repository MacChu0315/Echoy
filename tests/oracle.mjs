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
const thresholdOffsets = [-.001, 0, .001];
const bars = [0, 1, 2, 3, 4];
const duplicates = [0, 1, 2];
const eventFields = ['step', 'kind', 'vel', 'note', 'dur', 'accent', 'expression', 'notes', 'harmony', 'chord', 'articulation', 'maxDuration'];
const requiredFeedbackBranches = [
  'motif-recovery', 'play-preparing', 'sync-failed', 'sync-waiting', 'syncing',
  'waiting-beat', 'pending-role', 'pending-phrase', 'pending-activity', 'moving',
  'contact-confirming', 'arrangement-pending', 'notice', 'link-split', 'link-hold',
  'normal-source', 'normal-leader', 'normal-shared-solo', 'normal-independent',
  'paused-source', 'paused-shared', 'paused-independent',
];

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

function activitySamples(role) {
  const aroundThresholds = thresholdSamples.flatMap(threshold =>
    thresholdOffsets.map(offset => Number((threshold + offset).toFixed(3))));
  return [...new Set([0, ...aroundThresholds, defaults[role], 1])].sort((a, b) => a - b);
}

function generateMusic(sandbox) {
  const engine = new sandbox.DuijuEngine();
  const cases = [];
  let eventCount = 0;
  for (const role of roles) {
    const activities = activitySamples(role);
    assert.equal(activities.length, 45, `${role} activity samples must include every threshold neighbor`);
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
      activitySamples: {
        thresholds: thresholdSamples,
        defaults,
        endpoints: [0, 1],
        neighborRule: { epsilon: .001, offsets: thresholdOffsets, rounding: 'three decimal places' },
        expandedByRole: Object.fromEntries(roles.map(role => [role, activitySamples(role)])),
      },
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
  const feedbackText = {
    'motif-recovery': { 'zh-CN': '声音已准备 · 下一小节恢复', en: 'Sound ready · Resumes next bar' },
    'play-preparing': { 'zh-CN': '准备播放 · 可点暂停取消', en: 'Preparing · Tap Pause to cancel' },
    'sync-failed': { 'zh-CN': '暂未对齐 · 继续原来的演奏', en: 'Not synced · Keeping current music' },
    'sync-waiting': { 'zh-CN': '已约定拍点 · 下一小节接入', en: 'Beat agreed · Joins next bar' },
    syncing: { 'zh-CN': '正在对拍 · 原演奏继续', en: 'Syncing · Current music continues' },
    'waiting-beat': { 'zh-CN': '等待下一拍 · 可点暂停取消', en: 'Next beat · Tap Pause to cancel' },
    'pending-role': { 'zh-CN': '和声 · 分解 待生效', en: 'HARMONY · Arpeggio · Pending' },
    'pending-phrase': { 'zh-CN': '轻答 · 下一小节生效', en: 'Light reply · Next bar' },
    'pending-activity': { 'zh-CN': '新节奏 · 下一小节生效', en: 'New rhythm · Next bar' },
    moving: { 'zh-CN': '位置确认中 · 保持当前编排', en: 'Checking position · Keeping pattern' },
    'contact-confirming': { 'zh-CN': '接触待确认', en: 'Confirming contact' },
    'arrangement-pending': { 'zh-CN': '新关系 · 下一小节生效', en: 'New connection · Next bar' },
    notice: { 'zh-CN': '已生效', en: 'Applied' },
    'link-split': { 'zh-CN': '无线失联 · 本地独奏', en: 'Link lost · Playing solo' },
    'link-hold': { 'zh-CN': '同伴失联 · 暂时保持拍子', en: 'Peer lost · Holding the beat' },
    'normal-source': { 'zh-CN': '回应左邻', en: 'Replying to the left' },
    'normal-leader': { 'zh-CN': '提出动机', en: 'Leading the phrase' },
    'normal-shared-solo': { 'zh-CN': '独奏 · 保留同场拍子', en: 'Solo · Keeping the shared beat' },
    'normal-independent': { 'zh-CN': '独奏', en: 'Solo' },
    'paused-source': { 'zh-CN': '已暂停 · 回应关系保留', en: 'Paused · Response retained' },
    'paused-shared': { 'zh-CN': '已暂停 · 同场拍子保留', en: 'Paused · Shared beat retained' },
    'paused-independent': { 'zh-CN': '已暂停 · 独奏就绪', en: 'Paused · Ready for solo' },
  };
  const states = [
    ...roles.map(role => ({ id: `perform-${role}`, route: 'perform', role, phrase: role === 'harmony' ? 2 : 1, activity: defaults[role], state: 'paused', ...(role === 'pulse' ? { feedbackBranch: 'paused-independent' } : {}) })),
    { id: 'perform-playing', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'normal-independent' },
    { id: 'perform-waiting', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'waiting', feedbackBranch: 'waiting-beat' },
    { id: 'perform-pending-activity', route: 'perform', role: 'dust', phrase: 1, activity: .78, committedActivity: .48, state: 'paused', feedbackBranch: 'pending-activity' },
    { id: 'feedback-motif-recovery', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'paused', feedbackBranch: 'motif-recovery' },
    { id: 'feedback-play-preparing', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'paused', feedbackBranch: 'play-preparing' },
    { id: 'feedback-sync-failed', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'playing', feedbackBranch: 'sync-failed' },
    { id: 'feedback-syncing', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'playing', feedbackBranch: 'syncing' },
    { id: 'feedback-sync-waiting', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'playing', feedbackBranch: 'sync-waiting' },
    { id: 'feedback-pending-role', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'paused', feedbackBranch: 'pending-role' },
    { id: 'feedback-pending-phrase', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'paused', feedbackBranch: 'pending-phrase' },
    { id: 'feedback-moving', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'moving' },
    { id: 'feedback-contact-confirming', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'contact-confirming' },
    { id: 'feedback-arrangement-pending', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'arrangement-pending' },
    { id: 'feedback-notice', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'notice' },
    { id: 'feedback-link-split', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'link-split' },
    { id: 'feedback-link-hold', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'link-hold' },
    { id: 'feedback-normal-source', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'playing', feedbackBranch: 'normal-source' },
    { id: 'feedback-normal-leader', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'normal-leader' },
    { id: 'feedback-normal-shared-solo', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'playing', feedbackBranch: 'normal-shared-solo' },
    { id: 'feedback-paused-source', route: 'perform', role: 'voice', phrase: 1, activity: .56, state: 'paused', feedbackBranch: 'paused-source' },
    { id: 'feedback-paused-shared', route: 'perform', role: 'pulse', phrase: 1, activity: .58, state: 'paused', feedbackBranch: 'paused-shared' },
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
  const cases = ['zh-CN', 'en'].flatMap(locale => states.map(state => ({
    locale,
    ...state,
    ...(state.feedbackBranch ? { expectedFeedback: feedbackText[state.feedbackBranch][locale] } : {}),
  })));
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
  const uiCaseById = new Map(uiCases.cases.map(item => [`${item.locale}/${item.id}`, item]));
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
    const uiCase = uiCaseById.get(item.id);
    if (uiCase.feedbackBranch) {
      assert.equal(item.feedback.branch, uiCase.feedbackBranch, `${item.id} feedback branch was not captured`);
      assert.equal(item.feedback.text, uiCase.expectedFeedback, `${item.id} visible feedback changed`);
    }
    if (item.canvas.width) {
      const expectedSteps = [...new Set(item.motif.map(event => event.step))];
      assert.equal(item.canvas.observation, 'CanvasRenderingContext2D.arc calls at score y=210');
      assert.deepEqual(item.canvas.renderedEventSteps, expectedSteps, `${item.id} rendered score dots do not match its motif`);
    }
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
    assert.equal(moduleBytes.toString('utf8'), html.slice(begin, end), `${name} does not preserve the exact source bytes`);
  }
  const sandbox = loadOracle();
  const engine = new sandbox.DuijuEngine();
  const notes = engine.phrase(engine.devices.A, null, 0, 0);
  assert(notes.some(note => note.kind === 'kick' && note.step === 0));
  const session = new sandbox.DuijuSession(0);
  session.tick(1);
  assert.notEqual(session.devices.A.sessionId, session.devices.B.sessionId);

  const generated = generateAll();
  const feedbackCoverage = [...new Set(generated.uiCases.cases.filter(item => item.locale === 'zh-CN' && item.feedbackBranch).map(item => item.feedbackBranch))].sort();
  assert.deepEqual(feedbackCoverage, requiredFeedbackBranches.slice().sort(), 'UI cases do not cover every observable feedback branch');
  const committedMusic = readJson(musicPath);
  assert.deepEqual(committedMusic.dimensions.activitySamples.neighborRule, {
    epsilon: .001,
    offsets: [-.001, 0, .001],
    rounding: 'three decimal places',
  }, 'activity fixtures must record the threshold-neighbor rule');
  for (const threshold of [.25, .26, .30, .42, .52, .65, .70, .72, .74, .75, .76, .78, .80, .82]) {
    for (const role of roles) {
      const samples = committedMusic.dimensions.activitySamples.expandedByRole[role];
      assert(samples.includes(Number((threshold - .001).toFixed(3))), `${role} misses the left neighbor of ${threshold}`);
      assert(samples.includes(threshold), `${role} misses threshold ${threshold}`);
      assert(samples.includes(Number((threshold + .001).toFixed(3))), `${role} misses the right neighbor of ${threshold}`);
    }
  }
  assert.equal(committedMusic.caseCount, 48600, 'activity-neighbor matrix case count changed');
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
