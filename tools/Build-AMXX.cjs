'use strict';

// Layout adapter only. Manifest/dependency resolution and parallel compilation
// are the pinned amxx-builder core's responsibility.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { parseArgs } = require('node:util');
const root = path.resolve(__dirname, '..');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const relative = file => path.relative(root, file).split(path.sep).join('/');

function safe(file) {
  const full = path.resolve(root, file);
  const rel = path.relative(root, full);
  if (!rel || rel.startsWith('..') || path.isAbsolute(rel)) throw new Error(`Path outside workspace: ${full}`);
  for (let current = full; current !== path.dirname(current); current = path.dirname(current)) {
    if (fs.existsSync(current) && fs.lstatSync(current).isSymbolicLink()) throw new Error(`Linked path refused: ${current}`);
  }
  return full;
}

function filesUnder(directory) {
  const result = [];
  for (const entry of fs.readdirSync(safe(directory), { withFileTypes: true })) {
    const file = safe(path.join(directory, entry.name));
    if (entry.isDirectory()) result.push(...filesUnder(file));
    else if (entry.isFile()) result.push(file);
  }
  return result.sort();
}

function write(file, bytes) {
  file = safe(file);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, bytes);
}

function clearOwned(directory) {
  directory = safe(directory);
  // Validate descendants as well: amxb itself recursively clears its buildDir.
  if (fs.existsSync(directory)) filesUnder(directory);
  fs.rmSync(directory, { recursive: true, force: true });
  fs.mkdirSync(directory, { recursive: true });
}

function prepareCompiler(builder) {
  const AdmZip = require(path.join(builder, 'node_modules/adm-zip'));
  const cache = safe('.tools/amxx-builder-cache');
  const compilerDir = safe(path.join(cache, 'amxxpc/1.9.5303/windows'));
  const packages = [
    ['amxmodx-1.9.0-git5303-base-windows.zip',
      'dd5c0f64b3974ce60e9a35d5bec957e8e2db95ae6fa12e45f24563373f550364', '.tools/amxx-1.9.0.5303'],
    ['amxmodx-1.9.0-git5303-cstrike-windows.zip',
      '54c83a9c632c86ff4dcf90793b47950c7308ab1d96f57052dbcaa203bd2fef35', '.tools/amxx-1.9.0.5303'],
    ['reapi-bin-5.29.0.358.zip',
      'f33a7435540bea8706db3fa948e51a85b511f5b18c383b97402a204dc5419195', '.tools/reapi-5.29.0.358'],
  ];
  for (const [name, digest, directory] of packages) {
    const bytes = fs.readFileSync(safe(path.join('.tools/downloads', name)));
    if (sha(bytes) !== digest) throw new Error(`Pinned SDK archive hash mismatch: ${name}`);
    const prefix = 'addons/amxmodx/scripting/';
    for (const entry of new AdmZip(bytes).getEntries()) {
      const name = entry.entryName.replaceAll('\\', '/');
      if (entry.isDirectory || !name.startsWith(prefix)) continue;
      const rel = name.slice(prefix.length);
      if (!(rel.startsWith('include/') && rel.endsWith('.inc')) && !['amxxpc.exe', 'amxxpc32.dll'].includes(rel)) continue;
      const data = entry.getData();
      const installed = safe(path.join(directory, name));
      if (sha(fs.readFileSync(installed)) !== sha(data)) throw new Error(`SDK differs from pinned archive: ${relative(installed)}`);
      if (directory.includes('amxx-1.9')) {
        const dest = safe(path.join(compilerDir, rel));
        if (!fs.existsSync(dest) || sha(fs.readFileSync(dest)) !== sha(data)) write(dest, data);
      }
    }
  }
  write(path.join(compilerDir, '.complete'), '1.9.5303');
  process.env.AMXX_BUILDER_CACHE = cache;
  return { version: '1.9.0.5303', sha256: sha(fs.readFileSync(path.join(compilerDir, 'amxxpc.exe'))) };
}

async function build() {
  const { values } = parseArgs({ options: {
    plugin: { type: 'string', multiple: true, default: [] },
    include: { type: 'string', multiple: true, default: [] },
  } });
  const lock = JSON.parse(fs.readFileSync(safe('sources.lock.json'), 'utf8'));
  const builder = safe(lock.sources.AMXXBuilder.path);
  const { parseManifest, parseDepsLines } = require(path.join(builder, 'src/manifest'));
  const { applyLocalOverrides } = require(path.join(builder, 'src/local-sources'));
  const { runBuild } = require(path.join(builder, 'src/build-service'));
  const { on, EVENTS } = require(path.join(builder, 'src/events'));
  const { subscribeCompiledRendering } = require(path.join(builder, 'src/commands/compile-renderer'));
  const manifest = parseManifest(safe('amxbuild.yml'));
  const input = safe('build/amxx/builder-input');
  const work = safe('build/amxx/builder-output');
  if (manifest.amxmodx.version !== '1.9.5303' || safe(manifest.amxmodx.dir) !== input ||
      manifest.repos.length || manifest.assets.sources.length || manifest.pluginIni.enabled) {
    throw new Error('Keep the pinned compiler, staging path and compile-only settings in amxbuild.yml.');
  }
  manifest.globalDeps.push(...parseDepsLines(values.include.map((directory, index) => ({
    source: 'local', name: `extra-${index}`, path: safe(directory), include_path: '.',
  }))));
  applyLocalOverrides(manifest, process.env);
  for (const dep of manifest.globalDeps) {
    if (dep.source !== 'local') throw new Error('GoldCraft builds use local, pinned include dependencies.');
    filesUnder(safe(path.join(dep._localDir, dep.include_path || '.')));
  }

  const sourceRoot = safe('amxx');
  const inputs = filesUnder(sourceRoot).filter(file => /\.(sma|inc)$/i.test(file)).map(file => ({
    file, rel: path.relative(sourceRoot, file).split(path.sep).join('/'), data: fs.readFileSync(file),
  }));
  const sources = new Map();
  const headers = new Map();
  for (const item of inputs) {
    if (item.rel.endsWith('.sma')) {
      const name = path.basename(item.rel, '.sma');
      const key = name.toLowerCase();
      if (!/^[a-z0-9_]+$/i.test(name)) throw new Error(`Invalid plugin filename: ${item.rel}`);
      if (sources.has(key)) throw new Error(`Duplicate plugin name: ${sources.get(key).rel}, ${item.rel}`);
      sources.set(key, { ...item, name });
    }
    // Expose all modules' public headers. Keep the full tree too, including
    // quoted relative SMA includes such as the production buy-menu fixture.
    const match = item.rel.match(/^(?:[^/]+\/)?include\/(.+\.inc)$/i);
    if (match) {
      const key = match[1].toLowerCase();
      const old = headers.get(key);
      if (old && !old.data.equals(item.data)) throw new Error(`Conflicting public include: ${old.rel}, ${item.rel}`);
      headers.set(key, { ...item, include: match[1] });
    }
  }
  const selected = values.plugin.length ? [...new Set(values.plugin.map(name => name.toLowerCase()))].map(name => {
    if (!sources.has(name)) throw new Error(`Unknown plugin: ${name}`);
    return sources.get(name);
  }) : [...sources.values()];
  if (!selected.length) throw new Error('No SMA sources found under amxx/.');
  manifest.plugins.rules = [...selected.map(item => ({ match: item.rel, enabled: true, ini: false })),
    { match: '**/*.sma', enabled: false }];

  const compiler = prepareCompiler(builder);
  clearOwned(input);
  for (const item of inputs) write(path.join(input, 'scripting', item.rel), item.data);
  for (const item of headers.values()) write(path.join(input, 'scripting/include', item.include), item.data);
  // Never pass the native/Java build root to runBuild: it deletes this directory.
  if (fs.existsSync(work)) filesUnder(work);
  subscribeCompiledRendering();
  const events = [];
  on(EVENTS.COMPILED, event => {
    events.push(event);
    // The upstream success renderer hides warnings; retain them in this entry.
    if (event.ok && /\bwarning \d+:/i.test(event.output)) console.warn(event.output.trim());
  });
  const report = { builder: lock.sources.AMXXBuilder, compiler, ok: false, plugins: selected.map(item => item.name) };
  try {
    await runBuild(manifest, { buildDir: work, fetch: false, archive: false });
    if (events.length !== selected.length || events.some(event => !event.ok)) throw new Error('Incomplete compiler results.');
    // Only promote after the complete selected set succeeds. Failed bytecode
    // stays in builder-output; last-build.json continues to describe success.
    const built = selected.map(item => {
      const artifact = safe(path.join(work, 'amxmodx/plugins', item.rel.replace(/\.sma$/, '.amxx')));
      const data = fs.readFileSync(artifact);
      if (!data.length) throw new Error(`Missing bytecode: ${item.name}`);
      return { name: item.name, source: relative(item.file), sourceSha256: sha(item.data),
        artifact: `build/amxx/plugins/${item.name}.amxx`, sha256: sha(data), data };
    });
    for (const entry of built) write(entry.artifact, entry.data);
    write('build/amxx/last-build.json', JSON.stringify(built.map(({ data, ...entry }) => entry), null, 2) + '\n');
    report.ok = true;
    report.warnings = events.reduce((n, event) => n + (event.output.match(/\bwarning \d+:/gi) || []).length, 0);
    console.log(`Built ${built.length} plugin(s), ${report.warnings} warning(s) -> build/amxx/plugins`);
  } finally {
    report.results = events;
    write('build/amxx/last-builder.json', JSON.stringify(report, null, 2) + '\n');
  }
}

build().catch(error => { console.error(error.message); process.exitCode = 1; });
