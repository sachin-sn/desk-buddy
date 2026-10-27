'use strict';
const test = require('node:test');
const assert = require('node:assert');
const N = require('../notifications');
const { validateConfig } = require('../buddy-helper');

const XML = `<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict><key>app</key><string>com.tinyspeck.slackmacgap</string>
<key>date</key><date>2026-10-27T08:00:00Z</date><key>req</key><dict><key>body</key><string>secret message</string><key>durl</key><data>AAEC</data>
<key>subt</key><string>#general</string><key>titl</key><string>Priya &amp; Co &lt;ops&gt;</string></dict></dict></plist>`;

test('parses title and subtitle, decoding entities, ignoring the body and data values', () => {
  const r = N.parseNotificationXml(XML);
  assert.deepStrictEqual(r, { title: 'Priya & Co <ops>', subtitle: '#general' });
  assert.ok(!JSON.stringify(r).includes('secret'));
});
test('missing title gives an empty string', () => assert.strictEqual(N.parseNotificationXml('<dict></dict>').title, ''));
test('app names: known, unknown, empty', () => {
  assert.strictEqual(N.appName('com.tinyspeck.slackmacgap'), 'Slack');
  assert.strictEqual(N.appName('org.example.fooBar'), 'FooBar');
  assert.strictEqual(N.appName(''), 'Notification');
  assert.strictEqual(N.appName(undefined), 'Notification');
});
test('cleanText keeps plain ASCII, one line, and cuts long text', () => {
  assert.strictEqual(N.cleanText('  Café \u{1F600}\n ok  ', 40), 'Caf ok');
  assert.strictEqual(N.cleanText('x'.repeat(50), 10), 'xxxxxxx...');
  assert.strictEqual(N.cleanText(null, 10), '');
});

// a fake sqlite3 / plutil
function fakeExec(rows, { fail } = {}) {
  const calls = [];
  const fn = async (cmd, args, opts) => {
    calls.push(cmd);
    if (fail) throw Object.assign(new Error('x'), { stderr: fail });
    if (cmd === 'plutil') return XML;
    const sql = args[args.length - 1];
    if (/MAX\(rec_id\)/.test(sql)) return JSON.stringify([{ m: Math.max(0, ...rows.map(r => r.id)) }]);
    const after = Number(/rec_id > (\d+)/.exec(sql)[1]);
    const out = rows.filter(r => r.id > after);
    return out.length ? JSON.stringify(out) : '';
  };
  fn.calls = calls;
  return fn;
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

test('skips what is already there, forwards new rows from listed apps only, once each', async () => {
  const rows = [{ id: 5, bundle: 'com.tinyspeck.slackmacgap', hex: '00' }];
  const got = [], logs = [];
  const stop = N.startNotifications({ apps: ['com.tinyspeck.slackmacgap'], onNote: n => got.push(n), log: m => logs.push(m), everyMs: 20, exec: fakeExec(rows), dbPath: '/fake/db' });
  await sleep(80);
  assert.strictEqual(got.length, 0, 'old rows are not replayed');
  rows.push({ id: 6, bundle: 'com.tinyspeck.slackmacgap', hex: '00' }, { id: 7, bundle: 'com.other.App', hex: '00' }, { id: 8, bundle: 'com.other.App', hex: '00' });
  await sleep(120);
  stop();
  assert.deepStrictEqual(got.map(g => [g.app, g.title]), [['Slack', 'Priya & Co <ops>']]);
  assert.strictEqual(logs.filter(l => /com\.other\.App/.test(l)).length, 1, 'unlisted app is mentioned once');
});
test('"*" forwards every app', async () => {
  const rows = [];
  const got = [];
  const stop = N.startNotifications({ apps: ['*'], onNote: n => got.push(n.app), everyMs: 20, exec: fakeExec(rows), dbPath: '/fake/db' });
  await sleep(60);
  rows.push({ id: 1, bundle: 'com.apple.mail', hex: '00' });
  await sleep(100);
  stop();
  assert.deepStrictEqual(got, ['Mail']);
});
test('no apps listed: does nothing and never touches sqlite', async () => {
  const ex = fakeExec([]);
  const stop = N.startNotifications({ apps: [], onNote() {}, exec: ex, dbPath: '/fake/db' });
  await sleep(30); stop();
  assert.strictEqual(ex.calls.length, 0);
});
test('a permission error is explained once', async () => {
  const logs = [];
  const stop = N.startNotifications({ apps: ['*'], onNote() {}, log: m => logs.push(m), everyMs: 20, exec: fakeExec([], { fail: 'Error: authorization denied' }), dbPath: '/fake/db' });
  await sleep(120); stop();
  assert.strictEqual(logs.filter(l => /Full Disk Access/.test(l)).length, 1, logs.join('|'));
});
test('config: notifyApps validated', () => {
  const tok = 'a'.repeat(20);
  assert.deepStrictEqual(validateConfig({ token: tok }).config.notifyApps, []);
  assert.deepStrictEqual(validateConfig({ token: tok, notifyApps: ['com.a.B', '*'] }).config.notifyApps, ['com.a.B', '*']);
  assert.ok(validateConfig({ token: tok, notifyApps: 'slack' }).errors.some(e => /notifyApps/.test(e)));
  assert.ok(validateConfig({ token: tok, notifyApps: ['bad id!'] }).errors.some(e => /notifyApps/.test(e)));
});
