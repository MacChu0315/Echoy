#!/usr/bin/env node
// This catches changes to generated music, including event order, source
// attribution, density boundaries and pitch. Expectations run the frozen JS.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const baseline = readFileSync(join(root, 'reference/echoy-arrange-B-v3.html'), 'utf8');
assert.equal(createHash('sha256').update(baseline).digest('hex'),
  '241a0a58e0e33cb906b4bf637c2d55f219de988df2a683c68d68f767c7dee6b5',
  'The approved music oracle must remain unchanged');

// Extract complete original IIFEs, without editing or reimplementing their
// algorithms. No browser, audio, DOM, clocks or network is present in this VM.
const context = vm.createContext({});
for (const exportName of ['PulseLab', 'ReplyAB', 'RoleAccompaniment']) {
  const exportAt = baseline.indexOf(`root.${exportName}=api`);
  assert(exportAt >= 0, `Missing ${exportName} oracle module`);
  const start = baseline.lastIndexOf('(function(root)', exportAt);
  const endMarker = "})(typeof window!=='undefined'?window:globalThis);";
  const end = baseline.indexOf(endMarker, exportAt) + endMarker.length;
  assert(start >= 0 && end > exportAt, `Incomplete ${exportName} oracle module`);
  vm.runInContext(baseline.slice(start, end), context, { timeout: 1000 });
}
const compose = context.RoleAccompaniment.compose;
const kinds = ['kick', 'rim', 'hat', 'tone', 'grain', 'chord'];
const roles = ['pulse', 'voice', 'dust', 'harmony'];
const layers = ['anchor', 'echo', 'ending', 'support', 'detail'];
const articulations = [undefined, 'short', 'broken', 'answer'];

function expected(plan) {
  return {
    role: roles.indexOf(plan.role), phrase: plan.phrase,
    density: plan.density, duration: plan.duration,
    source_count: plan.source.length, count: plan.full.length,
    source: plan.source.map(n => ({ step: n.step, velocity: n.vel, kind: kinds.indexOf(n.kind) })),
    events: plan.full.map(n => ({
      step: n.step, velocity: n.vel, duration: n.dur,
      max_duration: n.maxDuration ?? 0, source_step: n.sourceStep ?? null,
      expression: n.expression ?? 0, kind: kinds.indexOf(n.kind),
      layer: layers.indexOf(n.layer ?? n.role),
      articulation: articulations.indexOf(n.articulation), note: n.note ?? 0,
      notes: Array.from(n.notes ?? []), harmony: Array.from(n.harmony ?? []),
    })),
  };
}

function compare(actual, want, path) {
  if (typeof want === 'number') {
    assert.equal(typeof actual, 'number', path);
    assert(Math.abs(actual - want) <= 1e-12 * Math.max(1, Math.abs(want)),
      `${path}: expected ${want}, got ${actual}`);
  } else if (Array.isArray(want)) {
    assert(Array.isArray(actual), path);
    assert.equal(actual.length, want.length, `${path}.length`);
    want.forEach((value, i) => compare(actual[i], value, `${path}[${i}]`));
  } else if (want !== null && typeof want === 'object') {
    assert.deepEqual(Object.keys(actual).sort(), Object.keys(want).sort(), `${path} fields`);
    for (const key of Object.keys(want)) compare(actual[key], want[key], `${path}.${key}`);
  } else {
    assert.equal(actual, want, path);
  }
}

let randomState = 0x41ec0a7;
function random() {
  randomState = (Math.imul(randomState, 1664525) + 1013904223) >>> 0;
  return randomState / 0x100000000;
}
function maskNotes(mask) {
  return Array.from({ length: 16 }, (_, step) => ({ step, kind: 'kick', vel: .74 }))
    .filter(n => mask & (1 << n.step));
}
const cases = [];
function add(source, density, label, mask = null) {
  for (let role = 0; role < 4; ++role) for (let phrase = 1; phrase <= 3; ++phrase) {
    cases.push({ source, density, role, phrase, mask, label: `${label}/${roles[role]}/${phrase}/${density}` });
  }
}
const masks = [0, 1, 2, 1 << 7, 1 << 14, 1 << 15, 5, 9, 0x101, 0x405,
  0x1085, 0x8003, 0x8104, 0xaaaa, 0x5555, 0x7fff, 0xffff];
const thresholds = [0, .45, .62, .7, .72, 1];
for (const factor of [2, 4, 5, 6, 8]) {
  for (let i = 0; i < factor; ++i) thresholds.push((i + .5) / factor);
}
const densities = [...new Set(thresholds.flatMap(n => [n - 1e-9, n,
  n + 1e-9, n - Number.EPSILON * Math.abs(n), n + Number.EPSILON * Math.abs(n)]))];
for (const mask of masks) for (const density of densities) {
  add(maskNotes(mask), density, `mask-${mask.toString(16)}`, mask);
}
for (let i = 0; i < 128; ++i) {
  const mask = Math.floor(random() * 65536);
  for (const density of [0, .5, 1, random()]) add(maskNotes(mask), density, `random-mask-${i}`, mask);
}

// Generic inputs exercise JS clean(): last duplicate wins, ties round upward,
// steps clamp, invalid kinds become kick, nonfinite velocities default to .73.
const rawCases = [
  [{ step: .49999999999999994, kind: 'rim', vel: .74 }],
  [{ step: .5, kind: 'hat', vel: .74 }, { step: 1.4999999999999998, kind: 'rim', vel: .5 }],
  [{ step: 7.5, kind: 'hat', vel: .31 }, { step: 8.49, kind: 'rim', vel: 2 },
    { step: -.5, kind: 'tone', vel: -.3 }, { step: 32, kind: 'kick', vel: NaN },
    { step: NaN, kind: 'hat', vel: .8 }, { step: Infinity, kind: 'rim', vel: .5 }],
  [{ step: 0, kind: 'rim', vel: .16 }, { step: 2, kind: 'hat', vel: .85 },
    { step: 7, kind: 'kick', vel: .4 }, { step: 12, kind: 'hat', vel: .73 }],
  Array.from({ length: 100 }, (_, i) => ({ step: i / 4 - 5, kind: kinds[i % kinds.length], vel: i / 60 })),
  [{ step: -Infinity, kind: 'kick', vel: .7 }],
];
for (let i = 0; i < 64; ++i) {
  rawCases.push(Array.from({ length: Math.floor(random() * 40) }, () => ({
    step: random() * 24 - 4, kind: kinds[Math.floor(random() * kinds.length)],
    vel: random() * 1.4 - .2,
  })));
}
for (const [i, source] of rawCases.entries()) {
  for (const density of [0, .45, .62, .7, .7000001, .72, .9, 1, NaN, Infinity, -Infinity]) {
    add(source, density, `raw-${i}`);
  }
}

const cSource = join(root, 'projects/echoy/main/echoy_music.c');
assert(existsSync(cSource), 'C accompaniment implementation is missing: recorded rhythm cannot produce a composed reply');
const build = mkdtempSync(join(tmpdir(), 'echoy-music-'));
let total = 0;
let maxEvents = 0;
const seen = { fractional: false, nullable: false, chord: false, overlap: false };
try {
  const binary = join(build, 'music_runner');
  const sanitizers = process.env.ECHOY_MUSIC_SANITIZE === '1'
    ? ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] : [];
  const compile = spawnSync(process.env.CC || 'cc', ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
    ...sanitizers, '-I', join(root, 'projects/echoy/main'),
    join(root, 'tests/music/runner.c'), cSource, '-lm', '-o', binary], { encoding: 'utf8' });
  assert.equal(compile.status, 0, `C runner build failed:\n${compile.stdout}${compile.stderr}`);
  const input = cases.map(c => {
      const header = `${c.role} ${c.phrase} ${c.density} ${c.mask === null ? 0 : 1} ${c.mask ?? c.source.length}`;
      return c.mask !== null ? header : header + '\n' + c.source.map(n =>
        `${n.step} ${n.vel} ${kinds.indexOf(n.kind)}`).join('\n');
    }).join('\n') + '\n';
  const run = spawn(binary, [], { stdio: ['pipe', 'pipe', 'pipe'] });
  let diagnostics = '';
  run.stderr.setEncoding('utf8');
  run.stderr.on('data', data => { diagnostics += data; });
  const finished = new Promise((resolveRun, reject) => {
    run.on('error', reject);
    run.on('close', status => resolveRun(status));
  });
  run.stdin.end(input);
  try {
    for await (const line of createInterface({ input: run.stdout, crlfDelay: Infinity })) {
      const row = JSON.parse(line);
      const c = cases[total];
      assert(c, 'C runner emitted an unexpected extra plan');
      const want = expected(compose(c.source, roles[c.role], c.phrase, c.density));
      compare(row, want, c.label);
      assert(row.count <= 64, 'The bounded event capacity was exceeded');
      maxEvents = Math.max(maxEvents, row.count);
      for (const e of row.events) {
        seen.fractional ||= e.step % 1 !== 0;
        seen.nullable ||= e.source_step === null;
        seen.chord ||= e.notes.length === 3;
        seen.overlap ||= e.layer !== 0 && row.events.some(a => a.layer === 0 && a.step === e.step);
      }
      ++total;
    }
    assert.equal(await finished, 0, `C runner failed:\n${diagnostics}`);
    assert.equal(diagnostics, '', 'Runner must not produce sanitizer diagnostics');
    assert.equal(total, cases.length, 'One result is required per input');
  } finally {
    if (run.exitCode === null) run.kill();
  }
  for (const [feature, hit] of Object.entries(seen)) assert(hit, `${feature} coverage was not exercised`);
  console.log(`PASS: ${total} C plans match frozen JavaScript; all 12 role/phrase combinations, ${densities.length} density boundary values, generic cleaning, randomized inputs, ordering and all represented note fields; max ${maxEvents}/64 events.`);
  console.log('PASS: invalid arguments, null pointers, bounded writes and deterministic repeat composition.');
} finally {
  rmSync(build, { recursive: true, force: true });
}
