'use strict';
// Runs calendar_proxy.gs in a Node VM with a fake CalendarApp/ContentService.
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

const SRC = fs.readFileSync(path.join(__dirname, '..', 'calendar_proxy.gs'), 'utf8');
const NOW = new Date('2026-10-25T09:00:00Z');
const H = 3600 * 1000;

const GuestStatus = { YES: 'YES', NO: 'NO', MAYBE: 'MAYBE', INVITED: 'INVITED', OWNER: 'OWNER' };
const Visibility = { DEFAULT: 'DEFAULT', PUBLIC: 'PUBLIC', PRIVATE: 'PRIVATE', CONFIDENTIAL: 'CONFIDENTIAL' };

// the script's own `new Date()` must see the same fixed "now" as the test data
class FakeDate extends Date {
  constructor(...a) { if (a.length === 0) super(NOW.getTime()); else super(...a); }
  static now() { return NOW.getTime(); }
}
// values created inside the VM have another realm's prototypes; compare plain copies
const plain = v => JSON.parse(JSON.stringify(v));
const up = sandbox => plain(vm.runInContext('upcoming_', sandbox)(NOW));

function ev(title, startMs, endMs, o = {}) {
  return {
    getTitle: () => title,
    getStartTime: () => new Date(startMs),
    getEndTime: () => new Date(endMs),
    isAllDayEvent: () => !!o.allDay,
    getMyStatus: () => o.status || GuestStatus.OWNER,
    getVisibility: () => o.vis || Visibility.DEFAULT,
  };
}

function load(calendars, overrides = '') {
  const logs = [];
  const sandbox = {
    CalendarApp: {
      GuestStatus, Visibility,
      getDefaultCalendar: () => calendars.primary,
      getCalendarById: id => calendars[id] || null,
    },
    ContentService: {
      MimeType: { JSON: 'JSON' },
      createTextOutput: text => ({ text, mime: null, setMimeType(m) { this.mime = m; return this; } }),
    },
    Logger: { log: m => logs.push(m) },
    JSON, Math, Date: FakeDate, String, Object,
  };
  vm.createContext(sandbox);
  vm.runInContext(SRC + '\n' + overrides, sandbox);
  return { sandbox, logs };
}
const cal = events => ({ getEvents: (from, to) => events.filter(e => e.getEndTime() > from && e.getStartTime() < to) });
const get = (sandbox, t) => JSON.parse(vm.runInContext('doGet', sandbox)({ parameter: t === undefined ? {} : { t } }).text);
const setToken = 'TOKEN = "secret-token-1234567890";';

test('refuses until TOKEN is changed from the placeholder', () => {
  const { sandbox } = load({ primary: cal([]) });
  const r = get(sandbox, 'CHANGE-ME-to-a-long-random-string');
  assert.equal(r.ok, false);
  assert.match(r.error, /set TOKEN/);
});

test('wrong, missing and near-miss tokens get no data', () => {
  const { sandbox } = load({ primary: cal([ev('Secret', NOW.getTime() + H, NOW.getTime() + 2 * H)]) }, setToken);
  for (const t of [undefined, '', 'nope', 'secret-token-123456789', 'secret-token-12345678900']) {
    const r = get(sandbox, t);
    assert.deepEqual(r, { ok: false, error: 'unauthorized' }, String(t));
  }
  assert.equal(get(sandbox, 'secret-token-1234567890').ok, true);
});

test('returns upcoming events sorted, as epoch seconds, JSON mime type', () => {
  const t0 = NOW.getTime();
  const { sandbox } = load({ primary: cal([
    ev('Later', t0 + 5 * H, t0 + 6 * H),
    ev('Soon', t0 + H, t0 + 1.5 * H),
  ]) }, setToken);
  const out = vm.runInContext('doGet', sandbox)({ parameter: { t: 'secret-token-1234567890' } });
  assert.equal(out.mime, 'JSON');
  const r = JSON.parse(out.text);
  assert.equal(r.ok, true);
  assert.deepEqual(r.events, [
    { t: 'Soon', s: (t0 + H) / 1000, e: (t0 + 1.5 * H) / 1000 },
    { t: 'Later', s: (t0 + 5 * H) / 1000, e: (t0 + 6 * H) / 1000 },
  ]);
});

test('keeps a meeting that is in progress, drops finished and out-of-window ones', () => {
  const t0 = NOW.getTime();
  const { sandbox } = load({ primary: cal([
    ev('Running', t0 - H, t0 + H),
    ev('Over', t0 - 3 * H, t0 - 2 * H),
    ev('Far away', t0 + 40 * H, t0 + 41 * H),
  ]) }, setToken);
  const titles = up(sandbox).map(e => e.t);
  assert.deepEqual(titles, ['Running']);
});

test('skips all-day, declined and zero-length events', () => {
  const t0 = NOW.getTime();
  const { sandbox } = load({ primary: cal([
    ev('Holiday', t0, t0 + 24 * H, { allDay: true }),
    ev('Declined', t0 + H, t0 + 2 * H, { status: GuestStatus.NO }),
    ev('Reminder', t0 + H, t0 + H),
    ev('Real', t0 + H, t0 + 2 * H, { status: GuestStatus.YES }),
    ev('Maybe', t0 + 3 * H, t0 + 4 * H, { status: GuestStatus.MAYBE }),
  ]) }, setToken);
  assert.deepEqual(up(sandbox).map(e => e.t), ['Real', 'Maybe']);
});

test('private and confidential events show as Busy; HIDE_TITLES hides everything', () => {
  const t0 = NOW.getTime();
  const events = [
    ev('Therapy', t0 + H, t0 + 2 * H, { vis: Visibility.PRIVATE }),
    ev('Merger talks', t0 + 2 * H, t0 + 3 * H, { vis: Visibility.CONFIDENTIAL }),
    ev('Lunch', t0 + 3 * H, t0 + 4 * H, { vis: Visibility.PUBLIC }),
  ];
  let { sandbox } = load({ primary: cal(events) }, setToken);
  assert.deepEqual(up(sandbox).map(e => e.t), ['Busy', 'Busy', 'Lunch']);
  ({ sandbox } = load({ primary: cal(events) }, setToken + 'HIDE_TITLES = true;'));
  assert.deepEqual(up(sandbox).map(e => e.t), ['Busy', 'Busy', 'Busy']);
});

test('SHOW_PRIVATE_TITLES reveals private titles; HIDE_TITLES still wins', () => {
  const t0 = NOW.getTime();
  const events = [
    ev('Therapy', t0 + H, t0 + 2 * H, { vis: Visibility.PRIVATE }),
    ev('Merger talks', t0 + 2 * H, t0 + 3 * H, { vis: Visibility.CONFIDENTIAL }),
    ev('Lunch', t0 + 3 * H, t0 + 4 * H, { vis: Visibility.PUBLIC }),
  ];
  let { sandbox } = load({ primary: cal(events) }, setToken + 'SHOW_PRIVATE_TITLES = true;');
  assert.deepEqual(up(sandbox).map(e => e.t), ['Therapy', 'Merger talks', 'Lunch']);
  ({ sandbox } = load({ primary: cal(events) }, setToken + 'SHOW_PRIVATE_TITLES = true; HIDE_TITLES = true;'));
  assert.deepEqual(up(sandbox).map(e => e.t), ['Busy', 'Busy', 'Busy']);
});

test('titles are tidied: whitespace collapsed, trimmed to 40, empty becomes (no title)', () => {
  const t0 = NOW.getTime();
  const { sandbox } = load({ primary: cal([
    ev('  Weekly \n  sync\t ', t0 + H, t0 + 2 * H),
    ev('x'.repeat(100), t0 + 2 * H, t0 + 3 * H),
    ev('', t0 + 3 * H, t0 + 4 * H),
  ]) }, setToken);
  const r = up(sandbox);
  assert.equal(r[0].t, 'Weekly sync');
  assert.equal(r[1].t.length, 40);
  assert.equal(r[2].t, '(no title)');
});

test('caps the list at MAX_EVENTS and keeps the earliest', () => {
  const t0 = NOW.getTime();
  const many = [];
  for (let i = 20; i >= 1; i--) many.push(ev('E' + i, t0 + i * 0.5 * H, t0 + i * 0.5 * H + 0.25 * H));
  const { sandbox } = load({ primary: cal(many) }, setToken);
  const r = up(sandbox);
  assert.equal(r.length, 8);
  assert.equal(r[0].t, 'E1'); assert.equal(r[7].t, 'E8');
});

test('several calendars are merged and duplicates collapsed', () => {
  const t0 = NOW.getTime();
  const shared = () => ev('All hands', t0 + H, t0 + 2 * H);
  const { sandbox } = load({
    primary: cal([shared(), ev('Mine', t0 + 3 * H, t0 + 4 * H)]),
    'team@example.com': cal([shared(), ev('Team only', t0 + 2 * H, t0 + 3 * H)]),
  }, setToken + `CALENDAR_IDS = ['primary-ignored-by-fake', 'team@example.com'];`);
  // 'primary-ignored-by-fake' is not a known id -> skipped; use explicit ids for both:
  const both = load({
    primary: cal([shared(), ev('Mine', t0 + 3 * H, t0 + 4 * H)]),
    'me@example.com': cal([shared(), ev('Mine', t0 + 3 * H, t0 + 4 * H)]),
    'team@example.com': cal([shared(), ev('Team only', t0 + 2 * H, t0 + 3 * H)]),
  }, setToken + `CALENDAR_IDS = ['me@example.com', 'team@example.com', 'missing@example.com'];`);
  assert.deepEqual(up(both.sandbox).map(e => e.t), ['All hands', 'Team only', 'Mine']);
  assert.deepEqual(up(sandbox).map(e => e.t), ['All hands', 'Team only']);
});

test('an error inside Calendar becomes a short {ok:false} reply, not an exception', () => {
  const broken = { getEvents: () => { throw new Error('Service invoked too many times: ' + 'x'.repeat(300)); } };
  const { sandbox } = load({ primary: broken }, setToken);
  const r = get(sandbox, 'secret-token-1234567890');
  assert.equal(r.ok, false);
  assert.ok(r.error.length <= 120);
});

test('authorize() logs a preview and the reply fits what the buddy accepts (< 6000 bytes)', () => {
  const t0 = NOW.getTime();
  const many = [];
  for (let i = 1; i <= 20; i++) many.push(ev('Meeting with a fairly long name number ' + i, t0 + i * H, t0 + i * H + 0.5 * H));
  const { sandbox, logs } = load({ primary: cal(many) }, setToken);
  const out = vm.runInContext('doGet', sandbox)({ parameter: { t: 'secret-token-1234567890' } });
  assert.ok(Buffer.byteLength(out.text) < 6000, String(Buffer.byteLength(out.text)));
  vm.runInContext('authorize', sandbox)();
  assert.ok(logs.length >= 2);
});
