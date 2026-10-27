'use strict';
/*
 * Mac notifications -> the buddy.
 *
 * macOS keeps every notification it shows in a small SQLite database. This module watches that database for new
 * rows and hands the app name and title to a callback (the helper then posts them to the buddy). It never sends
 * the message text, and only for apps you list in config.json ("notifyApps").
 *
 * Reading the database needs "Full Disk Access" for whatever runs the helper (Terminal, or node for the LaunchAgent).
 * Without it macOS refuses and we say so once in the log instead of failing quietly.
 *
 * The pieces that parse and clean text are pure and unit-tested; the sqlite3 / plutil calls are injectable.
 */
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFile } = require('node:child_process');

const KNOWN_APPS = {
  'com.tinyspeck.slackmacgap': 'Slack',
  'net.whatsapp.WhatsApp': 'WhatsApp',
  'com.apple.mail': 'Mail',
  'com.apple.MobileSMS': 'Messages',
  'com.apple.iCal': 'Calendar',
  'com.apple.reminders': 'Reminders',
  'com.microsoft.teams2': 'Teams',
  'com.microsoft.teams': 'Teams',
  'com.microsoft.Outlook': 'Outlook',
  'ru.keepcoder.Telegram': 'Telegram',
  'com.hnc.Discord': 'Discord',
  'com.google.Chrome': 'Chrome',
  'com.apple.Safari': 'Safari',
  'us.zoom.xos': 'Zoom',
  'com.apple.FaceTime': 'FaceTime',
};

// "com.company.SomeApp" -> "SomeApp" when we do not know it
function appName(bundleId) {
  if (typeof bundleId !== 'string' || !bundleId) return 'Notification';
  if (KNOWN_APPS[bundleId]) return KNOWN_APPS[bundleId];
  const last = bundleId.split('.').pop() || bundleId;
  return last.charAt(0).toUpperCase() + last.slice(1);
}

function decodeXml(s) {
  return s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&apos;/g, "'")
    .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(Number(n))).replace(/&amp;/g, '&');
}

// The stored blob is a binary property list. plutil turns it into XML; we pull out the title and subtitle with
// a regular expression rather than a full parser, which also copes with the data/date values JSON cannot hold.
function parseNotificationXml(xml) {
  const grab = key => {
    const m = new RegExp('<key>' + key + '</key>\\s*<string>([\\s\\S]*?)</string>').exec(xml);
    return m ? decodeXml(m[1]) : '';
  };
  return { title: grab('titl'), subtitle: grab('subt') };
}

// A line safe for the buddy's screen (plain ASCII, one line). The buddy cleans again; this keeps the log tidy too.
function cleanText(s, max) {
  let t = String(s || '').replace(/[^\x20-\x7E]+/g, ' ').replace(/\s+/g, ' ').trim();
  if (t.length > max) t = t.slice(0, max - 3).trimEnd() + '...';
  return t;
}

function dbCandidates(home = os.homedir()) {
  return [
    path.join(home, 'Library', 'Group Containers', 'group.com.apple.usernoted', 'db2', 'db'),   // macOS 15 and later
  ];
}

function findDb(exists = fs.existsSync, extra = []) {
  for (const p of [...extra, ...dbCandidates()]) if (exists(p)) return p;
  return null;
}

const run = (cmd, args, opts = {}) => new Promise((resolve, reject) => {
  const child = execFile(cmd, args, { timeout: 4000, maxBuffer: 1 << 20, ...opts }, (err, out, errOut) => err ? reject(Object.assign(err, { stderr: errOut })) : resolve(out));
  if (opts.input !== undefined) child.stdin.end(opts.input);
});

// Rows newer than `afterId`: [{id, bundle, hex}] in order. `exec` is injectable for tests.
async function queryNew(db, afterId, exec = run) {
  const sql = `SELECT r.rec_id AS id, a.identifier AS bundle, hex(r.data) AS hex FROM record r JOIN app a ON a.app_id = r.app_id WHERE r.rec_id > ${Number(afterId) | 0} ORDER BY r.rec_id LIMIT 50;`;
  const out = await exec('sqlite3', ['-readonly', '-json', db, sql]);
  return out.trim() ? JSON.parse(out) : [];
}
async function maxId(db, exec = run) {
  const out = await exec('sqlite3', ['-readonly', '-json', db, 'SELECT IFNULL(MAX(rec_id),0) AS m FROM record;']);
  const rows = out.trim() ? JSON.parse(out) : [];
  return rows.length ? Number(rows[0].m) || 0 : 0;
}
async function decodeBlob(hex, exec = run) {
  const xml = await exec('plutil', ['-convert', 'xml1', '-o', '-', '-'], { input: Buffer.from(hex, 'hex') });
  return parseNotificationXml(xml);
}

/*
 * Starts watching. `apps` is the allow-list of bundle ids (or ['*'] for every app). `onNote({app, title, bundle})` is
 * called for each new notification from an allowed app. Returns a stop function.
 */
function startNotifications({ apps, onNote, log = () => {}, everyMs = 2000, exec = run, exists = fs.existsSync, dbPath = null }) {
  const list = new Set(apps || []);
  if (!list.size) return () => {};
  if (process.platform !== 'darwin' && !dbPath) { log('notifications: only on macOS, skipping'); return () => {}; }
  const db = dbPath || findDb(exists);
  if (!db) { log('notifications: cannot find the macOS notification database (needs macOS 15 or later)'); return () => {}; }
  const all = list.has('*');
  let last = null;                 // highest rec_id already handled; null until the first successful read
  let warned = false, busy = false;
  const seenOther = new Set();

  const tick = async () => {
    if (busy) return;
    busy = true;
    try {
      if (last === null) { last = await maxId(db, exec); log(`notifications: watching ${all ? 'all apps' : [...list].join(', ')}`); return; }   // skip what is already there
      for (const row of await queryNew(db, last, exec)) {
        last = Math.max(last, Number(row.id) || 0);
        const bundle = row.bundle || '';
        if (!all && !list.has(bundle)) {
          if (!seenOther.has(bundle)) { seenOther.add(bundle); log(`notifications: saw one from ${bundle} (not forwarded; add it to "notifyApps" to show it)`); }
          continue;
        }
        let title = '';
        try { title = (await decodeBlob(row.hex, exec)).title; } catch (_) { /* unreadable: still tell the buddy there was one */ }
        onNote({ app: cleanText(appName(bundle), 20), title: cleanText(title, 40), bundle });
      }
      warned = false;
    } catch (e) {
      const msg = String((e && e.stderr) || (e && e.message) || e);
      if (!warned) {
        warned = true;
        log(/authorization|not permitted|unable to open|operation not|denied/i.test(msg)
          ? 'notifications: macOS blocked reading them. System Settings -> Privacy & Security -> Full Disk Access -> add the program that runs the helper (Terminal, or node), then restart the helper'
          : `notifications: cannot read them (${msg.split('\n')[0]})`);
      }
    } finally { busy = false; }
  };
  tick();
  const timer = setInterval(tick, everyMs);
  timer.unref();
  return () => clearInterval(timer);
}

module.exports = { appName, parseNotificationXml, cleanText, decodeXml, dbCandidates, findDb, queryNew, maxId, decodeBlob, startNotifications, KNOWN_APPS };
