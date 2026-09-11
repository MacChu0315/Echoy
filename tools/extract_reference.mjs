#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const expectedHash = 'd23c51b025b44a0d2d11a3ad5a371abd4a2117adb8e742491e838af6dc4c0bb5';
const defaultSource = '/home/zhuzhengming/esp/Sync/Echoy_v5.0.3.html';
const boundaries = [
  ['response', '/* Complete-phrase response', '/* Harmony v5.0.2:'],
  ['harmony', '/* Harmony v5.0.2:', '/* Product rules:'],
  ['engine', '/* Product rules:', '/* Product session model.'],
  ['session', '/* Product session model.', '/* Simulator only:'],
];

function sha256(data) {
  return crypto.createHash('sha256').update(data).digest('hex');
}

function occurrences(text, marker) {
  const result = [];
  for (let offset = 0;;) {
    const found = text.indexOf(marker, offset);
    if (found < 0) return result;
    result.push(found);
    offset = found + marker.length;
  }
}

function sliceUnique(text, name, beginMarker, endMarker) {
  const begins = occurrences(text, beginMarker);
  const ends = occurrences(text, endMarker);
  if (begins.length !== 1) throw new Error(`${name}: expected one begin marker, found ${begins.length}`);
  if (ends.length !== 1) throw new Error(`${name}: expected one end marker, found ${ends.length}`);
  if (ends[0] <= begins[0]) throw new Error(`${name}: module boundaries reversed`);
  return text.slice(begins[0], ends[0]);
}

function build(source) {
  if (!fs.existsSync(source)) throw new Error(`reference source not found: ${source}`);
  const html = fs.readFileSync(source);
  const digest = sha256(html);
  if (digest !== expectedHash) throw new Error(`reference SHA-256 mismatch: expected ${expectedHash}, got ${digest}`);
  const text = html.toString('utf8');
  const version = text.match(/<title>[^<]*?([0-9]+\.[0-9]+\.[0-9]+)\b/);
  if (!version || version[1] !== '5.0.4') throw new Error(`embedded version mismatch: expected 5.0.4, got ${version?.[1] ?? 'none'}`);

  const files = new Map([['reference/Echoy_v5.0.3.html', html]]);
  const modules = {};
  for (const [name, beginMarker, endMarker] of boundaries) {
    const relative = `reference/extracted/${name}.js`;
    const contents = Buffer.from(sliceUnique(text, name, beginMarker, endMarker));
    files.set(relative, contents);
    modules[name] = {
      path: relative,
      beginMarker,
      endMarker,
      size: contents.length,
      sha256: sha256(contents),
    };
  }

  const manifest = {
    schemaVersion: 1,
    source,
    filenameVersion: '5.0.3',
    embeddedVersion: version[1],
    sha256: digest,
    size: html.length,
    storedPath: 'reference/Echoy_v5.0.3.html',
    storedMode: '0444',
    modules,
    references: {
      'esp-mosaico-vibe': { commit: 'f1a7d4a36ccc6ccdf1c0e73dbc9fb8c9e50b2753' },
      'esp-mosaico-bsp': { commit: '3d5c451598aaffbda6f41895a0414dba4bac3505' },
      'esp-mosaico-claw': { commit: '903fe11ec3ca4a8881afef104e53a8da040896ac' },
    },
  };
  files.set('reference/manifest.json', Buffer.from(JSON.stringify(manifest, null, 2) + '\n'));
  return { digest, files, manifest };
}

function writeStable(destination, data, mode = 0o644) {
  fs.mkdirSync(path.dirname(destination), { recursive: true });
  const current = fs.existsSync(destination) ? fs.readFileSync(destination) : null;
  if (!current || !current.equals(data)) {
    if (fs.existsSync(destination)) fs.chmodSync(destination, 0o644);
    fs.writeFileSync(destination, data);
  }
  fs.chmodSync(destination, mode);
}

function checkStable(file, expected) {
  assert(fs.existsSync(file), `missing extracted reference: ${path.relative(root, file)}`);
  assert(fs.readFileSync(file).equals(expected), `stale extracted reference: ${path.relative(root, file)}`);
}

function parseSource() {
  const index = process.argv.indexOf('--source');
  if (index < 0) return defaultSource;
  if (!process.argv[index + 1] || process.argv[index + 1].startsWith('--')) throw new Error('--source requires a path');
  return path.resolve(process.argv[index + 1]);
}

function selfTest(text) {
  for (const [name, begin, end] of boundaries) assert(sliceUnique(text, name, begin, end).startsWith(begin));
  const [name, begin, end] = boundaries[0];
  assert.throws(() => sliceUnique(text.replace(begin, ''), name, begin, end), /found 0/);
  assert.throws(() => sliceUnique(`${text}\n${begin}`, name, begin, end), /found 2/);
  assert.throws(() => sliceUnique(`${text}\n${end}`, name, begin, end), /found 2/);
}

const source = parseSource();
const result = build(source);
if (process.argv.includes('--self-test')) selfTest(fs.readFileSync(source, 'utf8'));

if (process.argv.includes('--check')) {
  for (const [relative, contents] of result.files) checkStable(path.join(root, relative), contents);
} else {
  for (const [relative, contents] of result.files) {
    const mode = relative === result.manifest.storedPath || relative.startsWith('reference/extracted/') ? 0o444 : 0o644;
    writeStable(path.join(root, relative), contents, mode);
  }
}

process.stdout.write(JSON.stringify({
  ok: true,
  mode: process.argv.includes('--check') ? 'check' : 'write',
  selfTest: process.argv.includes('--self-test'),
  sha256: result.digest,
  modules: Object.fromEntries(Object.entries(result.manifest.modules).map(([name, value]) => [name, value.sha256])),
}) + '\n');
