#!/usr/bin/env node
// Execute the frozen algorithms, rather than maintaining a second expectation
// implementation of linked music, detach policy or logical clock transitions.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const baseline = readFileSync(join(root, 'reference/echoy-arrange-B-v3.html'), 'utf8');
assert.equal(createHash('sha256').update(baseline).digest('hex'),
  '241a0a58e0e33cb906b4bf637c2d55f219de988df2a683c68d68f767c7dee6b5');
const context = vm.createContext({});
for (const exportName of ['PulseLab', 'ReplyAB', 'RoleAccompaniment', 'ArrangeModel']) {
  const marker = exportName === 'ArrangeModel' ? 'root.ArrangeModel=ArrangeModel' : `root.${exportName}=api`;
  const at = baseline.indexOf(marker), start = baseline.lastIndexOf('(function(root)', at);
  const endMarker = "})(typeof window!=='undefined'?window:globalThis);";
  const end = baseline.indexOf(endMarker, at) + endMarker.length;
  assert(at >= 0 && start >= 0 && end > at, `Missing original ${exportName} IIFE`);
  vm.runInContext(baseline.slice(start, end), context, { timeout: 2000 });
}
const ids = ['A', 'B', 'C'], roles = ['pulse', 'voice', 'dust', 'harmony'];
const kinds = ['kick', 'rim', 'hat', 'tone', 'grain', 'chord'];
const layers = ['anchor', 'echo', 'ending', 'support', 'detail'];
const articulations = [undefined, 'short', 'broken', 'answer'];
const toUs = value => Math.round(value * 1000000);
const mask = values => values.reduce((bits, id) => bits | (1 << ids.indexOf(id)), 0);
const seed = notes => Array.from(notes, n => ({ step: n.step, velocity: n.vel, kind: kinds.indexOf(n.kind) }));
function note(n) {
  return { step: n.step, velocity: n.vel, duration: n.dur, max_duration: n.maxDuration ?? 0,
    source_step: n.sourceStep ?? null, expression: n.expression ?? 0, kind: kinds.indexOf(n.kind),
    layer: layers.indexOf(n.layer ?? n.role), articulation: articulations.indexOf(n.articulation),
    note: n.note ?? 0, notes: Array.from(n.notes ?? []), harmony: Array.from(n.harmony ?? []) };
}
function music(p) {
  return { role: roles.indexOf(p.role), phrase: p.phrase, density: p.density, duration: p.duration,
    source: seed(p.rootSeed ?? p.source), events: Array.from(p.full, note), foreground: Array.from(p.foreground, note) };
}
function plan(p) {
  return p ? { source: ids.indexOf(p.source), detached: p.detached ?? null, music: music(p) } : null;
}
const edges = list => Array.from(list, e => [ids.indexOf(e.left), ids.indexOf(e.right)]);
function changes(m, job) {
  return ids.filter(id => job.plans[id]?.signature !== m.tiles[id].current?.signature ||
    job.clocks[id] && job.clocks[id] !== m.tiles[id].clockId);
}
function job(m, value, changed = changes(m, value)) {
  return { revision: value.revision, changed_mask: mask(Array.from(changed)), edges: edges(value.edges),
    clocks: ids.map(id => ids.indexOf(value.clocks[id])), plans: ids.map(id => plan(value.plans[id])) };
}
function state(m) {
  return { revision: m.revision, edges: edges(m.edges), wanted: edges(m.wantedEdges),
    pending: m.pending ? { at_us: toUs(m.pending.at), job: job(m, m.pending, m.pending.changes) } : null,
    clocks: ids.map(id => ({ origin_us: toUs(m.clocks[id].origin), started: m.clocks[id].started === true })),
    tiles: ids.map((id, index) => {
      const d = m.tiles[id];
      return { id: index, clock_id: ids.indexOf(d.clockId), role: roles.indexOf(d.role), phrase: d.phrase,
        density: d.density, present: d.present, loop: d.loop, use_own: d.useOwn,
        memories: roles.map(role => d.memories[role]), own_seed: seed(d.ownSeed), fallback: seed(d.fallback),
        current: plan(d.current) };
    }) };
}
function compare(actual, want, path) {
  if (typeof want === 'number') {
    assert.equal(typeof actual, 'number', path);
    assert(Math.abs(actual - want) <= 1e-12 * Math.max(1, Math.abs(want)),
      `${path}: expected ${want}, got ${actual}`);
  } else if (Array.isArray(want)) {
    assert(Array.isArray(actual), path); assert.equal(actual.length, want.length, `${path}.length`);
    want.forEach((value, i) => compare(actual[i], value, `${path}[${i}]`));
  } else if (want !== null && typeof want === 'object') {
    assert.deepEqual(Object.keys(actual).sort(), Object.keys(want).sort(), `${path} fields`);
    for (const key of Object.keys(want)) compare(actual[key], want[key], `${path}.${key}`);
  } else assert.equal(actual, want, path);
}

const build = mkdtempSync(join(tmpdir(), 'echoy-arrange-'));
const sanitize = process.env.ECHOY_ARRANGE_SANITIZE === '1';
let processC;
let diagnostics = '';
let operations = 0, linkedCases = 0;
const coverage = { detached: false, inheritedPitch: false, supportMoved: false, stale: false,
  boundary: false, noChangeCommit: false, fractional: false, reordered: false, silence: false,
  memory: false, independentMute: false, clockStayed: false, validation: false };
try {
  const binary = join(build, 'arrange_runner');
  const compiled = spawnSync(process.env.CC || 'cc', ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
    ...(sanitize ? ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] : []),
    '-I', join(root, 'projects/echoy/main'), join(root, 'tests/arrange/runner.c'),
    join(root, 'projects/echoy/main/echoy_arrange.c'), join(root, 'projects/echoy/main/echoy_music.c'),
    '-lm', '-o', binary], { encoding: 'utf8' });
  assert.equal(compiled.status, 0, `C build failed:\n${compiled.stdout}${compiled.stderr}`);
  processC = spawn(binary, [], { stdio: ['pipe', 'pipe', 'pipe'] });
  processC.stderr.setEncoding('utf8'); processC.stderr.on('data', text => { diagnostics += text; });
  const finished = new Promise((resolveRun, reject) => {
    processC.on('error', reject); processC.on('close', code => resolveRun(code));
  });
  const lines = createInterface({ input: processC.stdout, crlfDelay: Infinity })[Symbol.asyncIterator]();
  let model = new context.ArrangeModel(), saved = [null, null, null, null];
  async function exchange(command) {
    processC.stdin.write(command + '\n');
    const response = await lines.next();
    assert(!response.done, `C runner stopped after ${command}: ${diagnostics}`);
    return JSON.parse(response.value);
  }
  function rawText(notes) {
    return notes.length + ' ' + notes.map(n => `${n.step} ${n.vel} ${kinds.indexOf(n.kind)}`).join(' ');
  }
  async function operation(op, ...args) {
    let command = op, ok = true, value = null, committed = null, printedJob = null;
    try {
      if (op === 'R') { model = new context.ArrangeModel(); saved = [null, null, null, null]; }
      else if (op === 'S') { command += ` ${args[0]} ${rawText(args[1])}`; ok = model.setSeed(ids[args[0]], args[1]); }
      else if (op === 'C') {
        const [id, patch] = args;
        const flags = (patch.role !== undefined ? 1 : 0) | (patch.phrase !== undefined ? 2 : 0) | (patch.density !== undefined ? 4 : 0);
        command += ` ${id} ${flags} ${patch.role ?? 0} ${patch.phrase ?? 1} ${patch.density ?? .5}`;
        const js = { ...patch }; if (patch.role !== undefined) js.role = roles[patch.role] ?? 'invalid';
        model.configure(ids[id], js);
      } else if (op === 'P') { command += ` ${args[0]} ${args[1] ? 1 : 0}`; model.present(ids[args[0]], args[1]); }
      else if (op === 'E') {
        command += ` ${args[0].length} ${args[0].flat().join(' ')}`;
        model.setEdges(args[0].map(([left, right]) => ({ left: ids[left] ?? 'invalid', right: ids[right] ?? 'invalid' })));
      } else if (op === 'J') {
        command += ` ${args[0]}`; saved[args[0]] = model.compile(); printedJob = job(model, saved[args[0]]);
      } else if (op === 'T') {
        command += ` ${args[0]} ${args[1]}`;
        const result = saved[args[0]] && model.stage(saved[args[0]], args[1] / 1000000);
        ok = !!result;
        if (!ok && saved[args[0]]) coverage.stale = true;
        if (result && result.at > args[1] / 1000000 + .05) coverage.boundary = true;
      } else if (op === 'K') {
        command += ` ${args[0]}`;
        const pending = model.pending;
        ok = !!pending && args[0] / 1000000 + 1e-8 >= pending.at && pending.revision === model.revision;
        const updates = model.commit(args[0] / 1000000);
        if (ok) {
          committed = { revision: pending.revision, changed_mask: mask(Array.from(updates, u => u.id)), at_us: toUs(pending.at) };
          coverage.noChangeCommit ||= updates.length === 0;
          coverage.detached ||= ids.some(id => model.tiles[id].current?.detached);
        }
      } else if (op === 'L') {
        command += ` ${args[0]} ${args[1]}`;
        const result = model.enableLoop(ids[args[0]], args[1] / 1000000); ok = result !== null;
        if (ok) value = toUs(result);
      } else if (op === 'M') { command += ` ${args[0]}`; model.mute(ids[args[0]]); }
      else if (op === 'H') { command += ` ${args[0]} ${args[1]}`; value = model.phase(ids[args[0]], args[1] / 1000000); }
      else if (op === 'X') model.change();
      else assert.fail(`Unexpected operation ${op}`);
    } catch (error) {
      if (!['S', 'C', 'P', 'E', 'L', 'M'].includes(op)) throw error;
      ok = false; coverage.validation = true;
    }
    const actual = await exchange(command);
    compare(actual, { ok, value, commit: committed, job: printedJob, state: state(model) }, `operation ${operations}: ${command}`);
    ++operations;
    return actual;
  }
  async function apply(now) {
    await operation('J', 0); await operation('T', 0, now);
    const at = toUs(model.pending.at);
    await operation('K', at - 1); await operation('K', at);
    return at;
  }
  const maskNotes = mask => Array.from({ length: 16 }, (_, step) => ({ step, vel: .74, kind: 'kick' })).filter(n => mask & (1 << n.step));

  // Empty edge-only transactions, graph rejection before absent filtering,
  // cleaning and the prototype's ordered compound-patch failure behavior.
  await operation('R'); await apply(0);
  for (const invalid of [[[0, 0]], [[0, 1], [1, 0]], [[0, 1], [0, 2]], [[0, 2], [1, 2]],
    [[0, 1], [0, 1]], [[0, 1], [1, 2], [2, 0]], [[3, 0]], [[-1, 0]]]) await operation('E', invalid);
  await operation('S', 0, []);
  await operation('S', 0, [{ step: NaN, vel: .7, kind: 'kick' }, { step: Infinity, vel: .8, kind: 'rim' }]);
  await operation('S', 0, [{ step: .49999999999999994, vel: NaN, kind: 'tone' },
    { step: .5, vel: 2, kind: 'rim' }, { step: 1.49, vel: .16, kind: 'hat' },
    { step: 30, vel: -.4, kind: 'kick' }]);
  await operation('C', 0, { role: 4 }); await operation('C', 0, { role: 1, phrase: 4 });
  await operation('C', 0, { density: NaN }); await operation('C', 0, { density: Infinity });
  await operation('C', 0, { density: -Infinity }); await operation('C', 0, {});
  await operation('S', 4, maskNotes(1)); await operation('P', -1, true);
  await operation('C', 0, { phrase: 3 }); await operation('C', 0, { role: 3 });
  await operation('C', 0, { role: 1 });
  assert.equal(model.tiles.A.phrase, 3); coverage.memory = true;

  // Three voices inherit structured pitches, one clock and the same original
  // root seed. Attaching a silent tile never grants it looping permission.
  await operation('R'); await operation('S', 0, maskNotes(0x44a0));
  await apply(1000000); await operation('L', 0, 1100000);
  const originalClock = model.clocks.A.origin;
  await operation('P', 1, true); await operation('P', 2, true);
  await operation('E', [[0, 1], [1, 2]]); await apply(1200000);
  assert(!model.tiles.B.loop && !model.tiles.C.loop); coverage.silence = true;
  assert.equal(model.clocks.A.origin, originalClock); coverage.clockStayed = true;
  assert.equal(model.tiles.C.clockId, 'A'); assert.equal(model.tiles.C.current.source, 'B');
  await operation('L', 1, 3700000); await operation('L', 2, 3800000);
  await operation('M', 1); assert(model.tiles.A.loop && model.tiles.C.loop && !model.tiles.B.loop);
  coverage.independentMute = true;
  for (const id of [0, 1, 2]) await operation('H', id, 4249999);

  const beforeDetach = JSON.stringify(music(model.tiles.C.current));
  await operation('E', [[0, 1]]); await apply(4500000);
  assert.equal(JSON.stringify(music(model.tiles.C.current)), beforeDetach, 'detach retains exact linked notes');
  assert.equal(model.tiles.C.clockId, 'A'); assert(model.tiles.C.loop);
  await operation('C', 2, { density: .91 }); await apply(7000000);
  assert.equal(model.tiles.C.current.source, null); assert.equal(model.tiles.C.useOwn, false);
  await operation('S', 2, maskNotes(0x8003)); await apply(9500000);
  assert(model.tiles.C.useOwn);
  await operation('E', [[2, 0], [0, 1]]); await apply(12000000); coverage.reordered = true;
  await operation('P', 2, false); await apply(15000000); assert(!model.tiles.C.loop);
  await operation('P', 2, true); await operation('E', [[0, 1], [1, 2]]); await apply(16000000);

  // Compile work is immutable, revision-checked and cancelable. Mute alone
  // preserves the staged boundary and never resets a shared clock's origin.
  await operation('J', 1); await operation('C', 0, { role: 1 }); await operation('T', 1, 18000000);
  await operation('J', 2); await operation('T', 2, 18100000); await operation('X'); await operation('K', 30000000);
  await operation('T', 2, 30000000); await apply(30000000);
  await operation('C', 1, { role: 2, phrase: 1, density: 1 }); await operation('J', 3); await operation('T', 3, 33000000);
  const pendingTime = model.pending.at; await operation('M', 0); await operation('M', 2);
  assert.equal(model.pending.at, pendingTime); await operation('K', toUs(pendingTime));

  // Exhaust all local/upstream role and phrase pairs at low/high richness,
  // including fractional dust support and an initial-silence seed.
  for (const source of [maskNotes(0x0003), maskNotes(0x44a0), maskNotes(0x8181)]) {
    for (let role = 0; role < 4; ++role) for (let phrase = 1; phrase <= 3; ++phrase) {
      for (let upstreamRole = 0; upstreamRole < 4; ++upstreamRole) for (let upstreamPhrase = 1; upstreamPhrase <= 3; ++upstreamPhrase) {
        for (const density of [.2, 1]) {
          const local = context.RoleAccompaniment.compose(source, roles[role], phrase, density);
          const upstream = context.RoleAccompaniment.compose(source, roles[upstreamRole], upstreamPhrase, .85);
          const linked = context.ArrangeLinked(local, upstream);
          const command = `D ${role} ${phrase} ${density} ${upstreamRole} ${upstreamPhrase} .85 ${rawText(source)}`;
          const actual = await exchange(command);
          compare(actual, music(linked), `linked ${linkedCases}: ${command}`);
          ++linkedCases;
          coverage.fractional ||= actual.events.some(n => n.step % 1);
          coverage.inheritedPitch ||= linked.full.some((n, i) => n.note !== local.full[i]?.note);
          coverage.supportMoved ||= linked.full.some((n, i) => n.step !== local.full[i]?.step);
        }
      }
    }
  }

  // Deterministic stateful fuzzing compares every visible and pending field,
  // not just the final plan, after graphs/configuration/mutes/time interleave.
  let randomState = 0x41a22e;
  const random = () => ((randomState = (Math.imul(randomState, 1664525) + 1013904223) >>> 0) / 0x100000000);
  const graphs = [[], [[0, 1]], [[1, 2]], [[2, 0]], [[0, 1], [1, 2]], [[2, 1], [1, 0]],
    [[1, 0], [0, 2]], [[0, 2], [2, 1]], [[1, 2], [2, 0]], [[2, 0], [0, 1]]];
  for (let trial = 0; trial < 12; ++trial) {
    await operation('R'); let now = trial * 100000000;
    for (const id of [0, 1, 2]) { await operation('P', id, true); await operation('S', id, maskNotes(1 + Math.floor(random() * 65535))); }
    await apply(now);
    for (let step = 0; step < 100; ++step) {
      now += 1 + Math.floor(random() * 900000);
      const id = Math.floor(random() * 3), choice = Math.floor(random() * 11), slot = Math.floor(random() * 4);
      if (choice === 0) await operation('E', graphs[Math.floor(random() * graphs.length)]);
      else if (choice === 1) await operation('C', id, { role: Math.floor(random() * 4), phrase: 1 + Math.floor(random() * 3), density: random() });
      else if (choice === 2) await operation('S', id, maskNotes(Math.floor(random() * 65536)));
      else if (choice === 3) await operation('P', id, random() > .3);
      else if (choice === 4) await operation('J', slot);
      else if (choice === 5) await operation('T', slot, now);
      else if (choice === 6) await operation('K', now);
      else if (choice === 7) await operation('L', id, now);
      else if (choice === 8) await operation('M', id);
      else if (choice === 9) await operation('H', id, now);
      else if (random() < .3) await operation('X');
      else { await operation('J', slot); await operation('T', slot, now); await operation('K', now + 5000000); }
    }
  }
  processC.stdin.end();
  assert((await lines.next()).done, 'Unexpected extra C output');
  assert.equal(await finished, 0, diagnostics); assert.equal(diagnostics, '', 'Sanitizer diagnostics');
  for (const [name, hit] of Object.entries(coverage)) assert(hit, `${name} coverage missing`);
  console.log(`PASS${sanitize ? ' (ASan/UBSan)' : ''}: ${linkedCases} linked plans and ${operations} state transitions match unchanged ArrangeLinked/ArrangeModel JavaScript.`);
  console.log('PASS: chain validation, cleaned seeds, role memories, staged revisions, stale/cancelled jobs, edge-only commits, pitches/support relocation, exact detach, independent loop permission, clock preservation and randomized reordering.');
} finally {
  if (processC && processC.exitCode === null) processC.kill();
  rmSync(build, { recursive: true, force: true });
}
