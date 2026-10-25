#!/usr/bin/env node
'use strict';
/*
 * Desk Buddy - Mac helper
 *
 * A small local web service with no dependencies (Node 18+). The buddy calls it over your home network:
 *
 *   GET  /health                 -> {ok:true,...}
 *   POST /action {"id":"vscode"} -> runs the action called "vscode" from actions.json
 *   POST /notify {"title","body"} -> shows a macOS notification
 *
 * and this helper calls the buddy:
 *
 *   POST <deviceUrl>/api/stats {"cpu":12.5,"ram":61.0}   every few seconds, so the buddy can react to a busy Mac
 *
 * Safety model
 *   - The buddy only ever sends an action *id*. What the id does lives in actions.json on this Mac,
 *     so a leaked token can trigger your own short list of actions, never arbitrary commands.
 *   - Commands are started without a shell (argv arrays), so nothing in a request can be interpreted as shell code.
 *   - Requests need "Authorization: Bearer <token>", and are refused unless they come from your local network.
 *   - Small body limit, request rate limit, and a lockout after repeated wrong tokens.
 *
 * Usage:  node buddy-helper.js --help
 */
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const readline = require('node:readline');
const { spawn, execFile } = require('node:child_process');

const VERSION = '0.1.0';
const ID_RE = /^[A-Za-z0-9_-]{1,23}$/;            // same rule the buddy applies to "Mac action" ids
const TOKEN_RE = /^[A-Za-z0-9_-]{16,128}$/;
const MAX_BODY = 4096;
const RATE_WINDOW_MS = 10000;
const RATE_MAX = 30;                               // requests per window, all clients together
const FAIL_MAX = 10;                               // wrong tokens before a lockout
const FAIL_LOCK_MS = 60000;
const BLOCKED_URL_SCHEMES = new Set(['file:', 'javascript:', 'data:', 'vbscript:']);

// ------------------------------------------------------------------ network helpers
function isLanAddress(addr) {
  if (!addr) return false;
  let a = String(addr).toLowerCase();
  const mapped = /^::ffff:(\d+\.\d+\.\d+\.\d+)$/.exec(a);
  if (mapped) a = mapped[1];
  if (a === '::1') return true;
  if (net.isIPv4(a)) {
    const [p, q] = a.split('.').map(Number);
    return p === 10 || p === 127 || (p === 172 && q >= 16 && q <= 31) || (p === 192 && q === 168) || (p === 169 && q === 254);
  }
  if (net.isIPv6(a)) return /^f[cd][0-9a-f]{2}:/.test(a) || /^fe[89ab][0-9a-f]:/.test(a);   // ULA fc00::/7, link-local fe80::/10
  return false;
}

function sameSecret(a, b) {
  const ha = crypto.createHash('sha256').update(String(a)).digest();
  const hb = crypto.createHash('sha256').update(String(b)).digest();
  return crypto.timingSafeEqual(ha, hb);
}

// ------------------------------------------------------------------ config + actions
function validateConfig(raw, baseDir = __dirname) {
  const errors = [];
  const c = raw && typeof raw === 'object' ? raw : {};
  if (typeof c.token !== 'string' || !TOKEN_RE.test(c.token)) errors.push('token: copy it from the buddy web page (Calendar & Mac -> Access token); 16-128 letters/digits');
  const port = c.port === undefined ? 8765 : c.port;
  if (!Number.isInteger(port) || port < 1024 || port > 65535) errors.push('port: whole number between 1024 and 65535');
  const bind = c.bind === undefined ? '0.0.0.0' : c.bind;
  if (typeof bind !== 'string' || !net.isIP(bind)) errors.push('bind: an IP address such as 0.0.0.0');
  let deviceUrl = null;
  if (c.deviceUrl !== undefined && c.deviceUrl !== '') {
    try {
      const u = new URL(c.deviceUrl);
      if (u.protocol !== 'http:') throw new Error('not http');
      deviceUrl = u.origin;
    } catch (_) { errors.push('deviceUrl: something like http://buddy.local (the buddy only speaks plain http)'); }
  }
  const every = c.statsEverySec === undefined ? 10 : c.statsEverySec;
  if (typeof every !== 'number' || !(every >= 3 && every <= 300)) errors.push('statsEverySec: between 3 and 300');
  const actionsFile = path.resolve(baseDir, typeof c.actionsFile === 'string' && c.actionsFile ? c.actionsFile : 'actions.json');
  return { errors, config: { token: c.token, port, bind, deviceUrl, statsEverySec: every, actionsFile } };
}

function isStr(v, max = 200) { return typeof v === 'string' && v.length > 0 && v.length <= max; }

function validateActions(raw) {
  const errors = [];
  const actions = Object.create(null);
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return { errors: ['actions.json must be an object: {"id": {...}}'], actions };
  for (const [id, a] of Object.entries(raw)) {
    if (id.startsWith('//')) continue;                                  // "//": "comment" entries are ignored
    const at = `action "${id}": `;
    if (!ID_RE.test(id)) { errors.push(at + 'id may only use letters, digits, - and _ (max 23 characters)'); continue; }
    if (!a || typeof a !== 'object') { errors.push(at + 'must be an object with a "type"'); continue; }
    switch (a.type) {
      case 'open':
        if (!isStr(a.app, 100) || a.app.startsWith('-')) { errors.push(at + '"app" is the application name, e.g. "Visual Studio Code"'); continue; }
        if (a.files !== undefined && !(Array.isArray(a.files) && a.files.length <= 8 && a.files.every(f => isStr(f, 400) && !f.startsWith('-')))) { errors.push(at + '"files" must be a short list of paths'); continue; }
        break;
      case 'url': {
        let u;
        try { u = new URL(a.url); } catch (_) { errors.push(at + '"url" must be a full URL'); continue; }
        if (BLOCKED_URL_SCHEMES.has(u.protocol)) { errors.push(at + `${u.protocol} links are not allowed`); continue; }
        break;
      }
      case 'shortcut':
        if (!isStr(a.name, 100) || a.name.startsWith('-')) { errors.push(at + '"name" is the Shortcuts app shortcut name'); continue; }
        break;
      case 'exec':
        if (!(Array.isArray(a.argv) && a.argv.length >= 1 && a.argv.length <= 16 && a.argv.every(x => typeof x === 'string' && x.length <= 400) && a.argv[0])) {
          errors.push(at + '"argv" must be a list like ["/usr/bin/say", "hello"] (no shell)'); continue;
        }
        if (a.cwd !== undefined && !isStr(a.cwd, 400)) { errors.push(at + '"cwd" must be a folder path'); continue; }
        break;
      default:
        errors.push(at + 'type must be one of open, url, shortcut, exec'); continue;
    }
    if (a.timeoutMs !== undefined && !(Number.isInteger(a.timeoutMs) && a.timeoutMs >= 500 && a.timeoutMs <= 600000)) { errors.push(at + '"timeoutMs" must be 500-600000'); continue; }
    actions[id] = a;
  }
  return { errors, actions };
}

function actionToArgv(a) {
  switch (a.type) {
    case 'open': return ['open', '-a', a.app, ...(a.files || [])];
    case 'url': return ['open', a.url];
    case 'shortcut': return ['shortcuts', 'run', a.name];
    case 'exec': return a.argv.slice();
    default: throw new Error('unknown action type');
  }
}

function clean(s, max) {
  return String(s == null ? '' : s).replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, max);
}

// Title and body travel as separate argv entries, never inside the AppleScript text, so quotes in a
// meeting name cannot break out of the script.
function notifyArgv(title, body) {
  return ['osascript',
    '-e', 'on run argv',
    '-e', 'display notification (item 2 of argv) with title (item 1 of argv)',
    '-e', 'end run',
    clean(title, 60) || 'Desk Buddy', clean(body, 140)];
}

// ------------------------------------------------------------------ running commands
// Resolves quickly: ok once the command exits 0 or is still running after settleMs (long jobs such as a
// Shortcuts automation must not make the buddy wait), an error if it fails to start or exits non-zero early.
function realRunner(argv, opts = {}) {
  const { cwd, timeoutMs = 10000, settleMs = 1200 } = opts;
  return new Promise(resolve => {
    let done = false;
    let kill = null;
    let settle = null;
    const finish = r => { if (done) return; done = true; clearTimeout(settle); clearTimeout(kill); resolve(r); };
    let child;
    try {
      child = spawn(argv[0], argv.slice(1), { cwd, stdio: 'ignore', shell: false });
    } catch (e) { return finish({ ok: false, error: e.message }); }
    kill = setTimeout(() => { try { child.kill('SIGTERM'); } catch (_) { /* already gone */ } }, timeoutMs);
    child.on('error', e => finish({ ok: false, error: e.code === 'ENOENT' ? `command not found: ${argv[0]}` : e.message }));
    child.on('exit', (code, sig) => finish(code === 0 ? { ok: true } : { ok: false, error: `exited with ${code === null ? sig : 'code ' + code}` }));
    settle = setTimeout(() => finish({ ok: true, running: true }), settleMs);
  });
}

function dryRunner(log) {
  return async argv => { log(`[dry-run] would run: ${JSON.stringify(argv)}`); return { ok: true }; };
}

// ------------------------------------------------------------------ HTTP server
function readJson(req) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', c => {
      size += c.length;
      if (size > MAX_BODY) { reject(Object.assign(new Error('body too large'), { status: 413 })); req.destroy(); return; }
      chunks.push(c);
    });
    req.on('end', () => {
      try { resolve(chunks.length ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : {}); }
      catch (_) { reject(Object.assign(new Error('invalid JSON'), { status: 400 })); }
    });
    req.on('error', reject);
  });
}

function createServer({ config, actions, run, log = () => {}, now = Date.now }) {
  const hits = [];
  const fails = { n: 0, firstAt: 0, until: 0 };

  function reply(res, code, obj) {
    const body = JSON.stringify(obj);
    res.writeHead(code, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'Cache-Control': 'no-store' });
    res.end(body);
  }

  return http.createServer(async (req, res) => {
    const ip = req.socket.remoteAddress;
    try {
      if (!isLanAddress(ip)) { log(`refused ${ip}: not on the local network`); return reply(res, 403, { ok: false, error: 'local network only' }); }

      const t = now();
      while (hits.length && t - hits[0] > RATE_WINDOW_MS) hits.shift();
      if (hits.length >= RATE_MAX) return reply(res, 429, { ok: false, error: 'slow down' });
      hits.push(t);
      if (t < fails.until) return reply(res, 429, { ok: false, error: 'locked after too many wrong tokens' });

      const h = req.headers.authorization || '';
      if (!h.startsWith('Bearer ') || !sameSecret(h.slice(7), config.token)) {
        if (!fails.firstAt || t - fails.firstAt > FAIL_LOCK_MS) { fails.firstAt = t; fails.n = 0; }
        if (++fails.n >= FAIL_MAX) { fails.until = t + FAIL_LOCK_MS; fails.n = 0; fails.firstAt = 0; log(`locked for ${FAIL_LOCK_MS / 1000}s after repeated wrong tokens from ${ip}`); }
        return reply(res, 401, { ok: false, error: 'bad token' });
      }
      fails.n = 0; fails.firstAt = 0;

      const url = new URL(req.url, 'http://x');
      if (req.method === 'GET' && url.pathname === '/health') {
        return reply(res, 200, { ok: true, version: VERSION, host: os.hostname(), actions: Object.keys(actions).length });
      }
      if (req.method === 'POST' && url.pathname === '/action') {
        const body = await readJson(req);
        const id = body && body.id;
        if (typeof id !== 'string' || !ID_RE.test(id)) return reply(res, 400, { ok: false, error: 'missing or invalid id' });
        const a = actions[id];
        if (!a) { log(`unknown action "${id}"`); return reply(res, 404, { ok: false, error: `no action called "${id}" in actions.json` }); }
        const argv = actionToArgv(a);
        const r = await run(argv, { cwd: a.cwd, timeoutMs: a.timeoutMs || (a.type === 'shortcut' ? 60000 : 10000) });
        log(`action ${id} -> ${r.ok ? (r.running ? 'started' : 'ok') : 'FAILED ' + r.error}`);
        return reply(res, r.ok ? 200 : 500, r.ok ? { ok: true } : { ok: false, error: r.error });
      }
      if (req.method === 'POST' && url.pathname === '/notify') {
        const body = await readJson(req);
        const r = await run(notifyArgv(body && body.title, body && body.body), { timeoutMs: 5000 });
        log(`notify "${clean(body && body.title, 60)}" -> ${r.ok ? 'ok' : 'FAILED ' + r.error}`);
        return reply(res, r.ok ? 200 : 500, r.ok ? { ok: true } : { ok: false, error: r.error });
      }
      return reply(res, 404, { ok: false, error: 'not found' });
    } catch (e) {
      return reply(res, e.status || 500, { ok: false, error: e.status ? e.message : 'internal error' });
    }
  });
}

// ------------------------------------------------------------------ CPU / RAM stats pushed to the buddy
function cpuSample() {
  const s = { idle: 0, total: 0 };
  for (const c of os.cpus()) {
    const t = c.times;
    s.idle += t.idle;
    s.total += t.user + t.nice + t.sys + t.idle + t.irq;
  }
  return s;
}
function cpuPercent(prev, cur) {
  const dt = cur.total - prev.total;
  if (dt <= 0) return 0;
  return Math.max(0, Math.min(100, 100 * (1 - (cur.idle - prev.idle) / dt)));
}

// "Memory used" the way Activity Monitor roughly counts it: active + wired + compressed pages.
function parseVmStat(text, totalBytes) {
  const size = /page size of (\d+) bytes/.exec(text);
  if (!size) return null;
  const page = Number(size[1]);
  const get = label => { const m = new RegExp(label + ':\\s+(\\d+)').exec(text); return m ? Number(m[1]) : 0; };
  const used = (get('Pages active') + get('Pages wired down') + get('Pages occupied by compressor')) * page;
  return Math.max(0, Math.min(100, 100 * used / totalBytes));
}

function readRam() {
  return new Promise(resolve => {
    const fallback = () => resolve(100 * (1 - os.freemem() / os.totalmem()));
    if (process.platform !== 'darwin') return fallback();
    execFile('vm_stat', { timeout: 2000 }, (err, out) => {
      const p = err ? null : parseVmStat(out, os.totalmem());
      if (p === null) fallback(); else resolve(p);
    });
  });
}

function pushStats(deviceUrl, token, stats) {
  return new Promise(resolve => {
    const body = JSON.stringify({ cpu: Math.round(stats.cpu * 10) / 10, ram: Math.round(stats.ram * 10) / 10 });
    const u = new URL('/api/stats', deviceUrl);
    const req = http.request({
      hostname: u.hostname, port: u.port || 80, path: u.pathname, method: 'POST', timeout: 2500,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), Authorization: `Bearer ${token}` },
    }, res => { res.resume(); resolve(res.statusCode === 200); });
    req.on('timeout', () => { req.destroy(); resolve(false); });
    req.on('error', () => resolve(false));
    req.end(body);
  });
}

function startStats({ deviceUrl, token, everySec, log = () => {} }) {
  if (!deviceUrl) return () => {};
  let prev = cpuSample();
  let lastOk = null;
  const tick = async () => {
    const cur = cpuSample();
    const cpu = cpuPercent(prev, cur);
    prev = cur;
    const ok = await pushStats(deviceUrl, token, { cpu, ram: await readRam() });
    if (ok !== lastOk) { log(ok ? 'buddy is reachable, sending CPU/RAM' : `cannot reach the buddy at ${deviceUrl} (will keep trying)`); lastOk = ok; }
  };
  const timer = setInterval(tick, everySec * 1000);
  timer.unref();
  return () => clearInterval(timer);
}

// ------------------------------------------------------------------ command line
function loadJsonFile(file, what) {
  let text;
  try { text = fs.readFileSync(file, 'utf8'); }
  catch (e) { throw new Error(`cannot read ${what} (${file}): ${e.code === 'ENOENT' ? 'file not found - run: node buddy-helper.js --init' : e.message}`); }
  try { return JSON.parse(text); }
  catch (e) { throw new Error(`${what} (${file}) is not valid JSON: ${e.message}`); }
}

function loadAll(configPath) {
  const baseDir = path.dirname(configPath);
  const { config, errors } = validateConfig(loadJsonFile(configPath, 'config.json'), baseDir);
  if (errors.length) throw new Error('config.json:\n  - ' + errors.join('\n  - '));
  const a = validateActions(loadJsonFile(config.actionsFile, 'actions.json'));
  if (a.errors.length) throw new Error('actions.json:\n  - ' + a.errors.join('\n  - '));
  return { config, actions: a.actions };
}

function ask(question) {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  return new Promise(resolve => rl.question(question, a => { rl.close(); resolve(a.trim()); }));
}

async function cmdInit(args, configPath) {
  if (fs.existsSync(configPath) && !args.force) throw new Error(`${configPath} already exists (use --force to overwrite)`);
  let token = args.token;
  if (!token && process.stdin.isTTY) token = await ask('Paste the access token from the buddy web page (Calendar & Mac -> Access token): ');
  if (!token || !TOKEN_RE.test(token)) throw new Error('need the buddy\'s access token: node buddy-helper.js --init --token <token>');
  const cfg = { token, port: 8765, deviceUrl: args.device || 'http://buddy.local', statsEverySec: 10 };
  const { errors } = validateConfig(cfg);
  if (errors.length) throw new Error(errors.join('; '));
  fs.writeFileSync(configPath, JSON.stringify(cfg, null, 2) + '\n', { mode: 0o600 });
  const actionsPath = path.join(path.dirname(configPath), 'actions.json');
  if (!fs.existsSync(actionsPath)) fs.copyFileSync(path.join(__dirname, 'actions.example.json'), actionsPath);
  console.log(`Wrote ${configPath} and ${actionsPath}`);
  console.log('Next: edit actions.json to taste, then run  node buddy-helper.js --check  and  ./install-launchagent.sh');
}

function localNames() {
  const out = [];
  try {
    if (process.platform === 'darwin') {
      const n = require('node:child_process').execFileSync('scutil', ['--get', 'LocalHostName'], { timeout: 2000 }).toString().trim();
      if (n) out.push(`${n}.local`);
    }
  } catch (_) { /* fall through to os.hostname */ }
  if (!out.length) out.push(os.hostname());
  for (const list of Object.values(os.networkInterfaces())) for (const i of list || []) if (i.family === 'IPv4' && !i.internal) out.push(i.address);
  return out;
}

function usage() {
  console.log(`Desk Buddy Mac helper ${VERSION}

  node buddy-helper.js                 start the helper
  node buddy-helper.js --init [--token T] [--device http://buddy.local] [--force]
                                       create config.json (+ actions.json from the example)
  node buddy-helper.js --check         validate config.json and actions.json, list the actions
  node buddy-helper.js --dry-run       start, but only log what would run
  node buddy-helper.js --config FILE   use another config file (default: config.json next to this script)`);
}

function parseArgs(argv) {
  const a = { _: [] };
  for (let i = 0; i < argv.length; i++) {
    const k = argv[i];
    if (['--init', '--check', '--dry-run', '--force', '--help', '-h', '--version'].includes(k)) a[k.replace(/^-+/, '')] = true;
    else if (['--token', '--device', '--config'].includes(k)) a[k.slice(2)] = argv[++i];
    else a._.push(k);
  }
  return a;
}

async function main(argv) {
  const args = parseArgs(argv);
  if (args.help || args.h) return usage();
  if (args.version) return console.log(VERSION);
  if (args._.length) { usage(); throw new Error(`unknown argument: ${args._[0]}`); }
  const configPath = path.resolve(args.config || process.env.BUDDY_HELPER_CONFIG || path.join(__dirname, 'config.json'));

  if (args.init) return cmdInit(args, configPath);

  const { config, actions } = loadAll(configPath);
  if (args.check) {
    console.log(`config OK: port ${config.port}, device ${config.deviceUrl || '(none - no CPU/RAM push)'}`);
    for (const [id, a] of Object.entries(actions)) console.log(`  ${id.padEnd(14)} ${JSON.stringify(actionToArgv(a))}`);
    return console.log(`${Object.keys(actions).length} action(s) OK`);
  }

  const log = m => console.log(`${new Date().toISOString().replace('T', ' ').slice(0, 19)}  ${m}`);
  const server = createServer({ config, actions, run: args['dry-run'] ? dryRunner(log) : realRunner, log });
  server.on('error', e => { console.error(e.code === 'EADDRINUSE' ? `port ${config.port} is already in use (is the helper already running?)` : e.message); process.exit(1); });
  server.listen(config.port, config.bind, () => {
    log(`Desk Buddy helper ${VERSION} listening on ${config.bind}:${config.port}${args['dry-run'] ? ' (dry run)' : ''}`);
    log(`Mac address for the buddy web page: ${localNames().join('  or  ')}`);
    log(`${Object.keys(actions).length} action(s): ${Object.keys(actions).join(', ')}`);
  });
  const stopStats = startStats({ deviceUrl: config.deviceUrl, token: config.token, everySec: config.statsEverySec, log });
  const bye = () => { stopStats(); server.close(() => process.exit(0)); setTimeout(() => process.exit(0), 1500).unref(); };
  process.on('SIGINT', bye);
  process.on('SIGTERM', bye);
}

module.exports = { isLanAddress, validateConfig, validateActions, actionToArgv, notifyArgv, clean, realRunner, dryRunner,
  createServer, cpuPercent, parseVmStat, pushStats, startStats, parseArgs, VERSION };

if (require.main === module) {
  main(process.argv.slice(2)).catch(e => { console.error('Error: ' + e.message); process.exit(1); });
}
