'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const os = require('node:os');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const H = require('../buddy-helper.js');

const TOKEN = 'abcdef0123456789abcdef01';

// ------------------------------------------------------------------ helpers
function listen(server) {
  return new Promise(resolve => server.listen(0, '127.0.0.1', () => resolve(server.address().port)));
}
function call(port, method, pathname, { token = TOKEN, body, raw, headers = {} } = {}) {
  return new Promise((resolve, reject) => {
    const payload = raw !== undefined ? raw : body !== undefined ? JSON.stringify(body) : null;
    const h = { ...headers };
    if (token !== null) h.Authorization = `Bearer ${token}`;
    if (payload !== null) { h['Content-Type'] = 'application/json'; h['Content-Length'] = Buffer.byteLength(payload); }
    const req = http.request({ host: '127.0.0.1', port, method, path: pathname, headers: h }, res => {
      let data = '';
      res.on('data', c => { data += c; });
      res.on('end', () => { let json = null; try { json = JSON.parse(data); } catch (_) { /* not json */ } resolve({ status: res.statusCode, json }); });
    });
    req.on('error', reject);
    if (payload !== null) req.write(payload);
    req.end();
  });
}
const ACTIONS = H.validateActions({
  vscode: { type: 'open', app: 'Visual Studio Code' },
  github: { type: 'url', url: 'https://github.com' },
  focus: { type: 'shortcut', name: 'Focus' },
  hi: { type: 'exec', argv: ['/usr/bin/say', 'hi'] },
}).actions;

async function withServer(opts, fn) {
  const calls = [];
  const run = opts.run || (async (argv, o) => { calls.push({ argv, o }); return { ok: true }; });
  const server = H.createServer({ config: { token: TOKEN }, actions: ACTIONS, run, now: opts.now });
  const port = await listen(server);
  try { await fn(port, calls); } finally { await new Promise(r => server.close(r)); }
}

// ------------------------------------------------------------------ LAN check
test('isLanAddress accepts private/loopback/link-local and rejects public', () => {
  for (const ok of ['127.0.0.1', '10.1.2.3', '172.16.0.1', '172.31.255.1', '192.168.1.50', '169.254.3.4', '::1', '::ffff:192.168.0.9', 'fd12:3456::1', 'fe80::1']) {
    assert.equal(H.isLanAddress(ok), true, ok);
  }
  for (const bad of ['8.8.8.8', '172.32.0.1', '172.15.0.1', '192.169.1.1', '100.64.0.1', '2001:db8::1', '::ffff:8.8.8.8', '', undefined, 'localhost']) {
    assert.equal(H.isLanAddress(bad), false, String(bad));
  }
});

// ------------------------------------------------------------------ config
test('validateConfig applies defaults and rejects bad values', () => {
  const ok = H.validateConfig({ token: TOKEN, deviceUrl: 'http://buddy.local' }, '/base');
  assert.deepEqual(ok.errors, []);
  assert.equal(ok.config.port, 8765);
  assert.equal(ok.config.deviceUrl, 'http://buddy.local');
  assert.equal(ok.config.statsEverySec, 10);
  assert.equal(ok.config.actionsFile, path.resolve('/base', 'actions.json'));

  assert.ok(H.validateConfig({}).errors.some(e => e.startsWith('token')));
  assert.ok(H.validateConfig({ token: 'short' }).errors.some(e => e.startsWith('token')));
  assert.ok(H.validateConfig({ token: TOKEN, port: 80 }).errors.some(e => e.startsWith('port')));
  assert.ok(H.validateConfig({ token: TOKEN, port: '8765' }).errors.some(e => e.startsWith('port')));
  assert.ok(H.validateConfig({ token: TOKEN, bind: 'not-an-ip' }).errors.some(e => e.startsWith('bind')));
  assert.ok(H.validateConfig({ token: TOKEN, deviceUrl: 'https://buddy.local' }).errors.some(e => e.startsWith('deviceUrl')));
  assert.ok(H.validateConfig({ token: TOKEN, deviceUrl: 'nonsense' }).errors.some(e => e.startsWith('deviceUrl')));
  assert.ok(H.validateConfig({ token: TOKEN, statsEverySec: 1 }).errors.some(e => e.startsWith('statsEverySec')));
  assert.equal(H.validateConfig({ token: TOKEN, deviceUrl: '' }).config.deviceUrl, null);
});

// ------------------------------------------------------------------ actions
test('validateActions accepts each type and builds argv without a shell', () => {
  const r = H.validateActions({
    '//': 'comment ignored',
    a1: { type: 'open', app: 'Terminal' },
    a2: { type: 'open', app: 'TextEdit', files: ['/tmp/x.txt'] },
    a3: { type: 'url', url: 'https://example.com/?q=a b' },
    a4: { type: 'url', url: 'obsidian://open?vault=Notes' },
    a5: { type: 'shortcut', name: 'Focus Mode' },
    a6: { type: 'exec', argv: ['/bin/echo', 'a; rm -rf /', '$(whoami)'], cwd: '/tmp' },
  });
  assert.deepEqual(r.errors, []);
  assert.equal(Object.keys(r.actions).length, 6);
  assert.deepEqual(H.actionToArgv(r.actions.a1), ['open', '-a', 'Terminal']);
  assert.deepEqual(H.actionToArgv(r.actions.a2), ['open', '-a', 'TextEdit', '/tmp/x.txt']);
  assert.deepEqual(H.actionToArgv(r.actions.a3), ['open', 'https://example.com/?q=a b']);
  assert.deepEqual(H.actionToArgv(r.actions.a5), ['shortcuts', 'run', 'Focus Mode']);
  assert.deepEqual(H.actionToArgv(r.actions.a6), ['/bin/echo', 'a; rm -rf /', '$(whoami)']);
});

test('validateActions rejects unsafe or malformed entries', () => {
  const bad = {
    'bad id!': { type: 'open', app: 'X' },
    noType: {},
    badType: { type: 'sh', cmd: 'ls' },
    dash: { type: 'open', app: '-a' },
    nourl: { type: 'url', url: 'not a url' },
    file: { type: 'url', url: 'file:///etc/passwd' },
    js: { type: 'url', url: 'javascript:alert(1)' },
    emptyArgv: { type: 'exec', argv: [] },
    strArgv: { type: 'exec', argv: 'ls -la' },
    numArgv: { type: 'exec', argv: ['ls', 5] },
    longId012345678901234567: { type: 'open', app: 'X' },
    timeout: { type: 'open', app: 'X', timeoutMs: 5 },
    noName: { type: 'shortcut' },
  };
  const r = H.validateActions(bad);
  assert.equal(Object.keys(r.actions).length, 0, JSON.stringify(Object.keys(r.actions)));
  assert.equal(r.errors.length, Object.keys(bad).length, r.errors.join('\n'));
  assert.ok(H.validateActions([]).errors.length === 1);
  assert.ok(H.validateActions(null).errors.length === 1);
});

test('the shipped actions.example.json and config.example.json are valid', () => {
  const actions = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'actions.example.json'), 'utf8'));
  const r = H.validateActions(actions);
  assert.deepEqual(r.errors, []);
  for (const id of ['vscode', 'terminal', 'chrome']) assert.ok(r.actions[id], id);       // ids used by the default Dev page
  const cfg = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'config.example.json'), 'utf8'));
  cfg.token = TOKEN;
  assert.deepEqual(H.validateConfig(cfg).errors, []);
});

// ------------------------------------------------------------------ notifications
test('notifyArgv keeps text out of the AppleScript source', () => {
  const evil = 'x" & (do shell script "touch /tmp/pwned") & "';
  const a = H.notifyArgv(evil, 'line1\nline2\u0007' + 'z'.repeat(300));
  assert.equal(a[0], 'osascript');
  const scriptParts = a.slice(1, 7).filter((x, i) => i % 2 === 1);
  assert.deepEqual(scriptParts, ['on run argv', 'display notification (item 2 of argv) with title (item 1 of argv)', 'end run']);
  assert.equal(a[7], evil.slice(0, 60));
  assert.ok(a[8].length <= 140 && !/[\n\u0007]/.test(a[8]));
  assert.equal(H.notifyArgv('', '')[7], 'Desk Buddy');
});

// ------------------------------------------------------------------ server
test('auth: missing or wrong token is refused, right token works', async () => {
  await withServer({}, async port => {
    assert.equal((await call(port, 'GET', '/health', { token: null })).status, 401);
    assert.equal((await call(port, 'GET', '/health', { token: 'wrong-wrong-wrong-wrong' })).status, 401);
    assert.equal((await call(port, 'GET', '/health', { headers: { Authorization: `Basic ${TOKEN}` }, token: null })).status, 401);
    const ok = await call(port, 'GET', '/health');
    assert.equal(ok.status, 200);
    assert.equal(ok.json.ok, true);
    assert.equal(ok.json.actions, 4);
  });
});

test('POST /action runs exactly the argv from actions.json', async () => {
  await withServer({}, async (port, calls) => {
    let r = await call(port, 'POST', '/action', { body: { id: 'vscode' } });
    assert.equal(r.status, 200);
    assert.deepEqual(calls[0].argv, ['open', '-a', 'Visual Studio Code']);
    r = await call(port, 'POST', '/action', { body: { id: 'focus' } });
    assert.deepEqual(calls[1].argv, ['shortcuts', 'run', 'Focus']);
    assert.equal(calls[1].o.timeoutMs, 60000);
    r = await call(port, 'POST', '/action', { body: { id: 'hi' } });
    assert.deepEqual(calls[2].argv, ['/usr/bin/say', 'hi']);
    assert.equal(calls.length, 3);
  });
});

test('POST /action: unknown id, invalid id, bad JSON, wrong method', async () => {
  await withServer({}, async (port, calls) => {
    assert.equal((await call(port, 'POST', '/action', { body: { id: 'nope' } })).status, 404);
    assert.equal((await call(port, 'POST', '/action', { body: { id: '../etc' } })).status, 400);
    assert.equal((await call(port, 'POST', '/action', { body: { id: 5 } })).status, 400);
    assert.equal((await call(port, 'POST', '/action', { body: {} })).status, 400);
    assert.equal((await call(port, 'POST', '/action', { raw: '{not json' })).status, 400);
    assert.equal((await call(port, 'GET', '/action')).status, 404);
    assert.equal((await call(port, 'POST', '/elsewhere', { body: {} })).status, 404);
    assert.equal(calls.length, 0, 'nothing may run for rejected requests');
  });
});

test('POST /action reports a failing command as 500', async () => {
  await withServer({ run: async () => ({ ok: false, error: 'command not found: open' }) }, async port => {
    const r = await call(port, 'POST', '/action', { body: { id: 'vscode' } });
    assert.equal(r.status, 500);
    assert.match(r.json.error, /command not found/);
  });
});

test('oversized bodies are cut off', async () => {
  await withServer({}, async port => {
    let status = 0;
    try { status = (await call(port, 'POST', '/notify', { raw: JSON.stringify({ title: 'x'.repeat(10000) }) })).status; }
    catch (e) { status = e.code; }                 // the server may also just drop the connection
    assert.ok(status === 413 || status === 'ECONNRESET' || status === 'EPIPE', String(status));
  });
});

test('POST /notify passes sanitised title and body to osascript', async () => {
  await withServer({}, async (port, calls) => {
    const r = await call(port, 'POST', '/notify', { body: { title: 'Standup in 10 min', body: 'Room "A"' } });
    assert.equal(r.status, 200);
    assert.equal(calls[0].argv[0], 'osascript');
    assert.equal(calls[0].argv[7], 'Standup in 10 min');
    assert.equal(calls[0].argv[8], 'Room "A"');
  });
});

test('rate limit: the 31st request inside 10 s is refused, then recovers', async () => {
  let t = 1000000;
  await withServer({ now: () => t }, async port => {
    for (let i = 0; i < 30; i++) assert.equal((await call(port, 'GET', '/health')).status, 200);
    assert.equal((await call(port, 'GET', '/health')).status, 429);
    t += 10001;
    assert.equal((await call(port, 'GET', '/health')).status, 200);
  });
});

test('lockout after 10 wrong tokens, even for the right token, until it expires', async () => {
  let t = 5000000;
  await withServer({ now: () => t }, async port => {
    for (let i = 0; i < 10; i++) { assert.equal((await call(port, 'GET', '/health', { token: 'wrong-token-wrong-token' })).status, 401); t += 300; }
    assert.equal((await call(port, 'GET', '/health')).status, 429);
    t += 61000;
    assert.equal((await call(port, 'GET', '/health')).status, 200);
  });
});

// ------------------------------------------------------------------ real runner (uses the host's /bin tools, not macOS ones)
test('realRunner: success, failure, missing command, long-running, timeout', async () => {
  assert.deepEqual(await H.realRunner(['/bin/echo', 'hi']), { ok: true });
  const f = await H.realRunner(['/bin/false']);
  assert.equal(f.ok, false); assert.match(f.error, /code 1/);
  const m = await H.realRunner(['/definitely/not/here']);
  assert.equal(m.ok, false); assert.match(m.error, /command not found/);
  const t0 = Date.now();
  const slow = await H.realRunner(['/bin/sleep', '5'], { settleMs: 200, timeoutMs: 300 });
  assert.equal(slow.ok, true); assert.equal(slow.running, true);
  assert.ok(Date.now() - t0 < 1500);
  // shell metacharacters in arguments are inert
  const marker = path.join(os.tmpdir(), `buddy-inert-${process.pid}`);
  await H.realRunner(['/bin/echo', `; touch ${marker}`]);
  assert.equal(fs.existsSync(marker), false);
});

// ------------------------------------------------------------------ stats
test('cpuPercent and parseVmStat', () => {
  assert.equal(H.cpuPercent({ idle: 0, total: 0 }, { idle: 50, total: 100 }), 50);
  assert.equal(H.cpuPercent({ idle: 10, total: 100 }, { idle: 10, total: 100 }), 0);
  assert.equal(H.cpuPercent({ idle: 0, total: 0 }, { idle: 0, total: 100 }), 100);
  const sample = `Mach Virtual Memory Statistics: (page size of 16384 bytes)
Pages free:                               20000.
Pages active:                            100000.
Pages inactive:                           90000.
Pages speculative:                         5000.
Pages wired down:                         50000.
Pages occupied by compressor:             10000.
`;
  const total = 16 * 1024 * 1024 * 1024;
  const expected = 100 * (160000 * 16384) / total;
  assert.ok(Math.abs(H.parseVmStat(sample, total) - expected) < 1e-9);
  assert.equal(H.parseVmStat('garbage', total), null);
  assert.equal(H.parseVmStat(sample, 1), 100);
});

test('pushStats posts JSON with the bearer token to the buddy', async () => {
  let seen = null;
  const device = http.createServer((req, res) => {
    let b = '';
    req.on('data', c => { b += c; });
    req.on('end', () => { seen = { method: req.method, url: req.url, auth: req.headers.authorization, body: JSON.parse(b) }; res.writeHead(200); res.end('{"ok":true}'); });
  });
  const port = await listen(device);
  try {
    assert.equal(await H.pushStats(`http://127.0.0.1:${port}`, TOKEN, { cpu: 12.345, ram: 61.04 }), true);
    assert.deepEqual(seen, { method: 'POST', url: '/api/stats', auth: `Bearer ${TOKEN}`, body: { cpu: 12.3, ram: 61 } });
  } finally { await new Promise(r => device.close(r)); }
  assert.equal(await H.pushStats(`http://127.0.0.1:${port}`, TOKEN, { cpu: 1, ram: 1 }), false);      // device gone
});

// ------------------------------------------------------------------ command line
test('--init writes a private config, --check validates, bad token is refused', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'buddy-helper-'));
  const cfg = path.join(dir, 'config.json');
  const script = path.join(__dirname, '..', 'buddy-helper.js');
  const run = (...a) => execFileSync(process.execPath, [script, ...a], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  assert.throws(() => run('--init', '--config', cfg, '--token', 'short'), /token/);
  run('--init', '--config', cfg, '--token', TOKEN, '--device', 'http://192.168.1.77');
  assert.equal(fs.statSync(cfg).mode & 0o077, 0, 'config.json must not be readable by others');
  const saved = JSON.parse(fs.readFileSync(cfg, 'utf8'));
  assert.equal(saved.token, TOKEN); assert.equal(saved.deviceUrl, 'http://192.168.1.77');
  assert.ok(fs.existsSync(path.join(dir, 'actions.json')));
  const out = run('--check', '--config', cfg);
  assert.match(out, /action\(s\) OK/);
  assert.match(out, /\["open","-a","Visual Studio Code"\]/);
  assert.throws(() => run('--init', '--config', cfg, '--token', TOKEN), /already exists/);
  fs.writeFileSync(path.join(dir, 'actions.json'), '{"x":{"type":"exec","argv":[]}}');
  assert.throws(() => run('--check', '--config', cfg), /argv/);
  fs.rmSync(dir, { recursive: true, force: true });
});
