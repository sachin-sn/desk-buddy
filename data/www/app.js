'use strict';
// Desk Buddy web editor. No build step: this file is served straight from the device's flash.

// ------------------------------------------------------------------ tiny helpers
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];

function h(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'text') e.textContent = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'value') e.value = v;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid != null) e.append(kid);
  return e;
}
const NS = 'http://www.w3.org/2000/svg';
function s(tag, attrs = {}, ...kids) {
  const e = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) if (v != null) e.setAttribute(k, v);
  for (const kid of kids.flat()) if (kid != null) e.append(kid);
  return e;
}

let toastTimer = 0;
function toast(msg, kind = '') {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'toast show ' + kind;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = 'toast ' + kind; }, kind === 'bad' ? 6000 : 2600);
}

// POST (anything with a body) always carries X-Buddy: the device refuses Basic-auth writes without it,
// which stops other websites from driving the device through your browser's saved login.
async function api(path, body) {
  const opt = { cache: 'no-store', headers: {} };
  if (body !== undefined) {
    opt.method = 'POST';
    opt.headers['Content-Type'] = 'application/json';
    opt.headers['X-Buddy'] = '1';
    opt.body = typeof body === 'string' ? body : JSON.stringify(body);
  }
  const r = await fetch(path, opt);
  let j = null;
  try { j = await r.json(); } catch (_) { /* not JSON */ }
  if (!r.ok) throw new Error((j && j.error) || `HTTP ${r.status}`);
  return j;
}

async function guarded(btn, fn) {
  if (btn) btn.disabled = true;
  try { return await fn(); }
  catch (e) { toast(e.message, 'bad'); }
  finally { if (btn) btn.disabled = false; }
}

// ------------------------------------------------------------------ shortcut syntax (mirrors src/keycombo.h; the device re-checks)
const MODS = new Set(['cmd', 'command', 'gui', 'win', 'super', 'meta', 'ctrl', 'control', 'alt', 'opt', 'option', 'shift']);
const KEYS = new Set(['enter', 'return', 'esc', 'escape', 'tab', 'space', 'backspace', 'bksp', 'delete', 'del', 'up', 'down', 'left', 'right',
  'home', 'end', 'pageup', 'pagedown', 'insert', 'capslock', 'plus', 'comma']);
const MEDIA = new Set(['playpause', 'play', 'pause', 'next', 'prev', 'previous', 'volup', 'voldown', 'mute', 'stop']);

function keyOk(t) {
  const l = t.toLowerCase();
  if (MODS.has(l) || KEYS.has(l)) return true;
  const m = /^f(\d{1,2})$/.exec(l);
  if (m && +m[1] >= 1 && +m[1] <= 24) return true;
  return t.length === 1 && /[a-z0-9\-=[\]'`\\./]/i.test(t);
}

function checkHid(str) {
  str = str.trim();
  if (!str) return 'type a shortcut, e.g. cmd+space';
  if (str.length >= 64) return 'too long (max 63 characters)';
  let n = 0, wait = 0;
  for (let seg of str.split(';')) {
    seg = seg.trim();
    if (!seg) continue;
    if (n >= 8) return 'too many steps (max 8)';
    const l = seg.toLowerCase();
    if (l.startsWith('wait:')) {
      let ms = parseInt(seg.slice(5), 10);
      if (Number.isNaN(ms)) ms = 0;
      wait += Math.min(1500, Math.max(10, ms));
      n++;
    } else if (l.startsWith('text:')) {
      const t = seg.slice(5).replace(/^ +/, '');
      if (!t) return 'text: needs some text';
      if (t.length >= 48) return 'text is too long';
      n++;
    } else if (l.startsWith('spot:')) {
      const a = seg.slice(5).replace(/^ +/, '');
      if (!a) return 'spot: needs an app name';
      if (a.length >= 48) return 'app name is too long';
      if (n + 5 > 8) return 'too many steps (max 8)';
      wait += 650;
      n += 5;
    } else if (MEDIA.has(l)) {
      n++;
    } else {
      const toks = seg.split('+');
      if (toks.length > 6) return 'too many keys in one combo';
      for (let t of toks) {
        t = t.trim();
        if (!t) return 'empty key in combo';
        if (!keyOk(t)) return `unknown key '${t}'`;
      }
      n++;
    }
  }
  if (!n) return 'type a shortcut, e.g. cmd+space';
  if (wait > 1500) return 'waits add up to more than 1.5 s';
  return '';
}

const MAC_ID = /^[A-Za-z0-9_-]{1,23}$/;
function slotError(sl) {
  if (!sl.label.trim() || sl.label.length > 6) return 'label needs 1-6 characters';
  if (sl.kind === 'mac') return MAC_ID.test(sl.value) ? '' : 'action id: letters, digits, - and _ (max 23)';
  return checkHid(sl.value);
}
function presetError(t) {
  const m = Number(t.min);
  if (!t.label.trim() || t.label.length > 9) return 'label needs 1-9 characters';
  if (!(m >= 1 && m <= 5999)) return 'minutes must be 1 to 5999';
  return '';
}

// ------------------------------------------------------------------ layout model
const S = { layout: { pages: [], presets: [] }, saved: '', cur: 0, status: {}, cfg: {} };
const MAX_JSON = 3800;

function normColor(c) { return /^#[0-9a-f]{6}$/i.test(c || '') ? c.toLowerCase() : '#00d1ff'; }

function fromDevice(j) {
  return {
    pages: (j.pages || []).map(p => ({
      name: p.name || '', color: normColor(p.color),
      slots: (p.slots || []).map(x => x.mac ? { label: x.label || '', kind: 'mac', value: x.mac } : { label: x.label || '', kind: 'hid', value: x.hid || '' }),
    })),
    presets: (j.presets || []).map(t => ({ label: t.label || '', min: t.min })),
  };
}
function toDevice(m) {
  return {
    pages: m.pages.map(p => ({
      name: p.name, color: p.color.toUpperCase(),
      slots: p.slots.map(x => x.kind === 'mac' ? { label: x.label, mac: x.value } : { label: x.label, hid: x.value }),
    })),
    presets: m.presets.map(t => ({ label: t.label, min: Number(t.min) })),
  };
}
const serialize = () => JSON.stringify(toDevice(S.layout));

function layoutProblems() {
  const L = S.layout, out = [];
  if (!L.pages.length) out.push('add at least one page');
  L.pages.forEach((p, i) => {
    if (!p.name.trim() || p.name.length > 11) out.push(`page ${i + 1}: name needs 1-11 characters`);
    p.slots.forEach((x, j) => { const e = slotError(x); if (e) out.push(`${p.name || 'page ' + (i + 1)} / ${j + 1}: ${e}`); });
  });
  L.presets.forEach((t, i) => { const e = presetError(t); if (e) out.push(`preset ${i + 1}: ${e}`); });
  if (serialize().length > MAX_JSON) out.push(`layout is ${serialize().length} characters, the limit is ${MAX_JSON}`);
  return out;
}

// ------------------------------------------------------------------ shortcuts tab
function renderShortcuts() {
  const L = S.layout;
  if (S.cur >= L.pages.length) S.cur = Math.max(0, L.pages.length - 1);

  const tabs = $('#pageTabs');
  tabs.replaceChildren(
    ...L.pages.map((p, i) => h('button', { class: 'chip' + (i === S.cur ? ' on' : ''), onclick: () => { S.cur = i; renderShortcuts(); } },
      h('i', { style: `background:${p.color}` }), p.name || '(unnamed)')),
    L.pages.length < 6 ? h('button', { class: 'chip add', onclick: addPage }, '+ Page') : null);

  const page = L.pages[S.cur];
  $('#pgName').disabled = $('#pgColor').disabled = $('#addSlot').disabled = $('#delPage').disabled = !page;
  if (!page) { $('#slots').replaceChildren(h('p', { class: 'muted', text: 'No pages yet.' })); refreshMeta(); return; }
  $('#pgName').value = page.name;
  $('#pgColor').value = page.color;
  $('#addSlot').disabled = page.slots.length >= 6;

  $('#slots').replaceChildren(...page.slots.map((sl, j) => slotRow(page, sl, j)));
  refreshMeta();
}

function slotRow(page, sl, j) {
  const label = h('input', { value: sl.label, maxlength: 6, placeholder: 'Label', 'aria-label': 'Label', 'data-slot': j,
    oninput: e => { sl.label = e.target.value; refreshMeta(); } });
  const kind = h('select', { 'aria-label': 'Type', onchange: e => { sl.kind = e.target.value; sl.value = ''; renderShortcuts(); $$('#slots .val input')[j]?.focus(); } },
    h('option', { value: 'hid', text: 'Keys', selected: sl.kind === 'hid' }),
    h('option', { value: 'mac', text: 'Mac action', selected: sl.kind === 'mac' }));
  const val = h('input', { value: sl.value, spellcheck: 'false', autocomplete: 'off', 'aria-label': 'Shortcut or action id',
    list: sl.kind === 'hid' ? 'keyHints' : null,
    placeholder: sl.kind === 'hid' ? 'cmd+space' : 'action id from actions.json',
    oninput: e => { sl.value = e.target.value; refreshMeta(); } });
  const move = d => () => { const a = page.slots, k = j + d; if (k < 0 || k >= a.length) return; [a[j], a[k]] = [a[k], a[j]]; renderShortcuts(); };
  const tools = h('div', { class: 'tools' },
    h('button', { class: 'ghost icon', title: 'Try it now', onclick: ev => testSlot(ev.target, sl) }, '▶'),
    h('button', { class: 'ghost icon', title: 'Move up', onclick: move(-1) }, '↑'),
    h('button', { class: 'ghost icon', title: 'Move down', onclick: move(1) }, '↓'),
    h('button', { class: 'ghost icon danger', title: 'Remove', onclick: () => { page.slots.splice(j, 1); renderShortcuts(); } }, '✕'));
  return h('div', { class: 'slot', 'data-j': j }, label, kind, h('div', { class: 'val' }, val), tools, h('div', { class: 'err', hidden: true }));
}

async function testSlot(btn, sl) {
  const e = slotError(sl);
  if (e) return toast(e, 'bad');
  await guarded(btn, async () => {
    await api('/api/test', sl.kind === 'mac' ? { mac: sl.value } : { hid: sl.value });
    toast('Sent', 'good');
  });
}

function addPage() {
  const colors = ['#00d1ff', '#4cd68a', '#b388ff', '#ff4fd8', '#ffb454', '#ff5d6c'];
  S.layout.pages.push({ name: 'Page ' + (S.layout.pages.length + 1), color: colors[S.layout.pages.length % colors.length], slots: [] });
  S.cur = S.layout.pages.length - 1;
  renderShortcuts();
}

function drawRing() {
  const svg = $('#ring');
  const page = S.layout.pages[S.cur];
  const kids = [s('circle', { cx: 120, cy: 120, r: 118, fill: '#050608', stroke: '#2a2d36', 'stroke-width': 2 })];
  if (page) {
    for (let i = 0; i < 6; i++) {
      const a = (-90 + 60 * i) * Math.PI / 180;
      const cx = 120 + Math.cos(a) * 70, cy = 120 + Math.sin(a) * 70;
      const sl = page.slots[i];
      if (!sl) { kids.push(s('circle', { cx, cy, r: 31, fill: 'none', stroke: '#2a2d36', 'stroke-dasharray': '3 4' })); continue; }
      const bad = slotError(sl);
      const g = s('g', { class: 'btn' },
        s('circle', { cx, cy, r: 31, fill: '#14161b', stroke: bad ? '#ff5d6c' : page.color, 'stroke-width': 3 }),
        s('text', { x: cx, y: cy }, sl.label || '?'),
        sl.kind === 'mac' ? s('circle', { cx: cx + 22, cy: cy - 22, r: 6, fill: page.color }) : null);
      g.addEventListener('click', () => $$('#slots input[data-slot]')[i]?.focus());
      kids.push(g);
    }
    kids.push(s('text', { x: 120, y: 114, class: 'pg' }, page.name), s('text', { x: 120, y: 130, class: 'pg' }, `${page.slots.length}/6`));
  }
  svg.replaceChildren(...kids);
}

function refreshMeta() {
  // inline errors (rows are not rebuilt while typing, so focus is never lost)
  const page = S.layout.pages[S.cur];
  if (page) $$('#slots .slot').forEach(row => {
    const sl = page.slots[+row.dataset.j];
    const e = sl ? slotError(sl) : '';
    const dirtyField = sl && (sl.value !== '' || sl.label !== '');
    const box = $('.err', row);
    box.textContent = dirtyField ? e : '';
    box.hidden = !(dirtyField && e);
    $('.val input', row).classList.toggle('bad', !!(dirtyField && e));
  });
  $$('#presets .preset').forEach(row => {
    const t = S.layout.presets[+row.dataset.i];
    const e = t ? presetError(t) : '';
    $('.err', row).textContent = e;
    $('.err', row).hidden = !e;
  });
  drawRing();
  $('#json').value = JSON.stringify(toDevice(S.layout), null, 1);
  const dirty = serialize() !== S.saved;
  const probs = layoutProblems();
  $('#savebar').hidden = !dirty;
  $('#savebar span').textContent = probs.length ? 'Fix: ' + probs[0] : 'Unsaved changes';
  $('#save').disabled = probs.length > 0;
}

$('#pgName').addEventListener('input', e => { const p = S.layout.pages[S.cur]; if (p) { p.name = e.target.value; $$('#pageTabs .chip')[S.cur]?.lastChild && ($$('#pageTabs .chip')[S.cur].lastChild.textContent = p.name || '(unnamed)'); refreshMeta(); } });
$('#pgColor').addEventListener('input', e => { const p = S.layout.pages[S.cur]; if (p) { p.color = e.target.value; const dot = $('#pageTabs .chip.on i'); if (dot) dot.style.background = p.color; refreshMeta(); } });
$('#addSlot').addEventListener('click', () => {
  const p = S.layout.pages[S.cur];
  if (!p || p.slots.length >= 6) return;
  p.slots.push({ label: '', kind: 'hid', value: '' });
  renderShortcuts();
  $$('#slots input[data-slot]').pop()?.focus();
});
$('#delPage').addEventListener('click', () => {
  const p = S.layout.pages[S.cur];
  if (!p || !confirm(`Delete page "${p.name}"?`)) return;
  S.layout.pages.splice(S.cur, 1);
  renderShortcuts();
});

$('#jsonCopy').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText($('#json').value); toast('Copied', 'good'); }
  catch (_) { $('#json').select(); toast('Press Ctrl/Cmd+C to copy'); }
});
$('#jsonApply').addEventListener('click', () => {
  try {
    const j = JSON.parse($('#json').value);
    if (!Array.isArray(j.pages)) throw new Error('missing "pages" array');
    S.layout = fromDevice(j);
    S.cur = 0;
    renderShortcuts(); renderPresets();
    toast('Loaded into the editor - press Save to send it to the buddy', 'good');
  } catch (e) { toast('Not valid: ' + e.message, 'bad'); }
});
$('#layoutReset').addEventListener('click', ev => guarded(ev.target, async () => {
  if (!confirm('Replace all shortcuts and timer presets with the built-in macOS defaults?')) return;
  await api('/api/layout/reset', {});
  await loadLayout();
  toast('Defaults restored', 'good');
}));

// ------------------------------------------------------------------ timers tab
function renderPresets() {
  const box = $('#presets');
  box.replaceChildren(...S.layout.presets.map((t, i) => h('div', { class: 'preset', 'data-i': i },
    h('input', { value: t.label, maxlength: 9, placeholder: 'Name', 'aria-label': 'Preset name', oninput: e => { t.label = e.target.value; refreshMeta(); } }),
    h('input', { type: 'number', min: 1, max: 5999, step: 'any', value: t.min, 'aria-label': 'Minutes', placeholder: 'minutes', oninput: e => { t.min = e.target.value; refreshMeta(); } }),
    h('button', { class: 'ghost icon danger', title: 'Remove', onclick: () => { S.layout.presets.splice(i, 1); renderPresets(); } }, '✕'),
    h('div', { class: 'err', hidden: true }))));
  $('#addPreset').disabled = S.layout.presets.length >= 6;
  refreshMeta();
}
$('#addPreset').addEventListener('click', () => { S.layout.presets.push({ label: '', min: 10 }); renderPresets(); $$('#presets .preset input').slice(-2)[0]?.focus(); });

// ------------------------------------------------------------------ save bar
$('#save').addEventListener('click', ev => guarded(ev.target, async () => {
  const probs = layoutProblems();
  if (probs.length) return toast(probs[0], 'bad');
  const body = serialize();
  await api('/api/layout', body);
  S.saved = body;
  refreshMeta();
  toast('Saved - the buddy updated its buttons', 'good');
}));
$('#discard').addEventListener('click', () => { S.layout = fromDevice(JSON.parse(S.saved)); renderShortcuts(); renderPresets(); });
window.addEventListener('beforeunload', e => { if (serialize() !== S.saved) { e.preventDefault(); e.returnValue = ''; } });

async function loadLayout() {
  const j = await api('/api/layout');
  S.layout = fromDevice(j);
  S.saved = serialize();
  renderShortcuts();
  renderPresets();
}

// ------------------------------------------------------------------ status pills + device card
function pill(text, cls) { return h('span', { class: 'pill ' + cls, text }); }

function renderStatus() {
  const st = S.status;
  $('#fw').textContent = st.fw ? 'v' + st.fw : '';
  $('#fwNow').textContent = st.fw ? 'v' + st.fw : '?';
  const pills = [];
  pills.push(st.ssid ? pill(`WiFi ${st.rssi} dBm`, st.rssi > -75 ? 'ok' : 'warn') : pill('No WiFi', 'bad'));
  pills.push(!st.bleEnabled ? pill('Bluetooth off', '') : st.ble ? pill('Mac paired', 'ok') : pill('Mac not paired', 'warn'));
  pills.push(!st.helperSet ? pill('Mac helper: not set', '') : st.helperOk ? pill('Mac helper', 'ok') : pill('Mac helper unreachable', 'warn'));
  pills.push({ ok: pill('Calendar', 'ok'), error: pill('Calendar error', 'bad'), pending: pill('Calendar…', 'warn') }[st.cal] || pill('Calendar: off', ''));
  pills.push(st.timeSynced ? pill('Clock synced', 'ok') : pill('Clock not synced', 'warn'));
  $('#pills').replaceChildren(...pills);

  const rows = [['Address', `${st.host}.local (${st.ip})`], ['Network', st.ssid || '—'], ['Uptime', fmtUp(st.uptime)],
    ['Free memory', `${Math.round(st.heap / 1024)} KB heap, ${Math.round(st.psram / 1048576 * 10) / 10} MB PSRAM`],
    ['Web files', `${Math.round(st.fsUsed / 1024)} KB of ${Math.round(st.fsTotal / 1048576)} MB used`]];
  $('#dev').replaceChildren(...rows.flatMap(([k, v]) => [h('dt', { text: k }), h('dd', { text: String(v) })]));
  $('#hostHint').textContent = (S.cfg.host || st.host || 'buddy') + '.local';
  $('#wxState').textContent = wxStatusText(st);
  renderWifi();
  $('#calState').textContent = { off: 'Calendar is off.', ok: 'Connected - events refresh every 5 minutes.', pending: st.timeSynced ? 'Fetching…' : 'Waiting for the buddy\'s clock to sync (it needs internet access).', error: 'Last fetch failed' + (st.calErr ? ': ' + st.calErr : '') + '. Check the URL and that the script is deployed for "Anyone". The Logs tab has the history.' }[st.cal] || '';
}
function fmtUp(sec) { sec = sec | 0; const d = Math.floor(sec / 86400), hh = Math.floor(sec % 86400 / 3600), m = Math.floor(sec % 3600 / 60); return (d ? d + 'd ' : '') + (hh || d ? hh + 'h ' : '') + m + 'm'; }

async function pollStatus() {
  try { S.status = await api('/api/status'); renderStatus(); } catch (_) { $('#pills').replaceChildren(pill('Buddy not reachable', 'bad')); }
}

// ------------------------------------------------------------------ calendar + Mac helper tab
$('#calSave').addEventListener('click', ev => guarded(ev.target, async () => {
  const url = $('#calUrl').value.trim();
  if (!url) return toast('Paste the web-app URL first, or use "Turn off"', 'bad');
  await api('/api/settings', { calUrl: url });
  $('#calUrl').value = '';
  $('#calUrl').placeholder = 'saved (hidden) - paste a new URL to replace it';
  toast('Saved - fetching events', 'good');
  setTimeout(pollStatus, 3000);
}));
$('#calClear').addEventListener('click', ev => guarded(ev.target, async () => {
  if (!confirm('Stop showing calendar events?')) return;
  await api('/api/settings', { calUrl: '' });
  $('#calUrl').placeholder = 'https://script.google.com/macros/s/…/exec?t=…';
  $('#agenda').replaceChildren();
  pollStatus();
}));
$('#calRefresh').addEventListener('click', ev => guarded(ev.target, async () => { await api('/api/calendar/refresh', {}); toast('Refreshing…'); setTimeout(pollStatus, 4000); }));
$('#calAgenda').addEventListener('click', ev => guarded(ev.target, async () => {
  const a = await api('/api/agenda');
  const fmt = new Intl.DateTimeFormat(undefined, { weekday: 'short', day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' });
  $('#agenda').replaceChildren(...(a.events.length
    ? a.events.map(e => h('li', {}, h('time', { text: fmt.format(new Date(e.s * 1000)) }), h('span', { text: e.t })))
    : [h('li', { class: 'muted', text: a.valid ? 'No upcoming events.' : 'Nothing fetched yet.' })]));
}));

$('#helperSave').addEventListener('click', ev => guarded(ev.target, async () => {
  await api('/api/settings', { helperHost: $('#helperHost').value.trim(), helperPort: +$('#helperPort').value || 8765 });
  toast('Saved', 'good');
  S.cfg.helperHost = $('#helperHost').value.trim();
  pollStatus();
}));

async function showToken() {
  const t = await api('/api/token');
  $('#token').value = t.token;
  const cfg = { token: t.token, port: +$('#helperPort').value || 8765, deviceUrl: `http://${t.host}.local` };
  const pre = $('#helperCfg');
  pre.textContent = '// helper/config.json\n' + JSON.stringify(cfg, null, 2);
  pre.hidden = false;
  $('#tokenShow').textContent = 'Hide';
}
$('#tokenShow').addEventListener('click', ev => guarded(ev.target, async () => {
  if (ev.target.textContent === 'Hide') { $('#token').value = ''; $('#helperCfg').hidden = true; ev.target.textContent = 'Show'; return; }
  await showToken();
}));
$('#tokenCopy').addEventListener('click', ev => guarded(ev.target, async () => {
  if (!$('#token').value) await showToken();
  try { await navigator.clipboard.writeText($('#token').value); toast('Token copied', 'good'); }
  catch (_) { $('#token').select(); toast('Press Ctrl/Cmd+C to copy'); }
}));
$('#tokenRegen').addEventListener('click', ev => guarded(ev.target, async () => {
  if (!confirm('Make a new token? The Mac helper will stop working until you paste the new token into its config.json.')) return;
  await api('/api/settings', { regenToken: true });
  await showToken();
  toast('New token created', 'good');
}));
$('#testMacGo').addEventListener('click', ev => guarded(ev.target, async () => {
  const id = $('#testMac').value.trim();
  if (!MAC_ID.test(id)) return toast('Type an action id first', 'bad');
  await api('/api/test', { mac: id });
  toast('Action sent to the Mac', 'good');
}));

// ------------------------------------------------------------------ settings tab
const TZ = [
  ['India (IST)', 'IST-5:30'], ['UTC', 'UTC0'], ['UK', 'GMT0BST,M3.5.0/1,M10.5.0'], ['Central Europe', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['US Eastern', 'EST5EDT,M3.2.0,M11.1.0'], ['US Central', 'CST6CDT,M3.2.0,M11.1.0'], ['US Pacific', 'PST8PDT,M3.2.0,M11.1.0'],
  ['Dubai', '<+04>-4'], ['Singapore', '<+08>-8'], ['Japan', 'JST-9'], ['Sydney', 'AEST-10AEDT,M10.1.0,M4.1.0/3'],
];
function fillTz() {
  $('#sTzPick').replaceChildren(...TZ.map(([n, v]) => h('option', { value: v, text: n })), h('option', { value: '', text: 'Custom…' }));
  $('#sTzPick').addEventListener('change', e => { if (e.target.value) $('#sTz').value = e.target.value; else $('#sTz').focus(); });
  $('#sTz').addEventListener('input', e => { $('#sTzPick').value = TZ.some(t => t[1] === e.target.value) ? e.target.value : ''; });
}
function bindRange(id, out) { $(id).addEventListener('input', e => { $(out).textContent = e.target.value; }); }
bindRange('#sBright', '#oBright'); bindRange('#sNight', '#oNight');

// ------------------------------------------------------------------ weather card (Settings tab)
// The city search runs in this browser (Open-Meteo's geocoding service); the buddy only ever gets the chosen coordinates.
const WX = { lat: 0, lon: 0, place: '' };
function wxChosenText() { return WX.place ? `${WX.place} (${(+WX.lat).toFixed(2)}, ${(+WX.lon).toFixed(2)})` : 'none'; }
function fillWeather() {
  const c = S.cfg;
  WX.lat = c.wLat || 0; WX.lon = c.wLon || 0; WX.place = c.wPlace || '';
  $('#wxUnit').value = c.wUnit === 'F' ? 'F' : 'C';
  $('#wxOn').checked = !!c.weatherOn;
  $('#wxChosen').textContent = wxChosenText();
}
function utf8Trim(s, maxBytes) {                   // the buddy stores at most 60 bytes of place name
  const enc = new TextEncoder();
  while (s && enc.encode(s).length > maxBytes) s = s.slice(0, -1);
  return s;
}
function pickCity(p, label) {
  WX.lat = p.latitude; WX.lon = p.longitude; WX.place = utf8Trim(label, 58);
  $('#wxChosen').textContent = wxChosenText();
  $('#wxOn').checked = true;
  $('#wxResults').replaceChildren();
}
$('#wxFind').addEventListener('click', ev => guarded(ev.target, async () => {
  const q = $('#wxQuery').value.trim();
  if (q.length < 2) throw new Error('Type at least two letters of the city name');
  let r;
  try { r = await fetch('https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&format=json&name=' + encodeURIComponent(q)); }
  catch (_) { throw new Error('Could not reach the city search - is this computer online?'); }
  if (!r.ok) throw new Error('City search failed (HTTP ' + r.status + ')');
  const res = (await r.json()).results || [];
  $('#wxResults').replaceChildren(...(res.length
    ? res.map(p => { const label = [p.name, p.admin1, p.country].filter(Boolean).join(', ');
        return h('li', {}, h('button', { class: 'ghost', type: 'button', text: label, onclick: () => pickCity(p, label) })); })
    : [h('li', { class: 'muted', text: 'No city found - try another spelling.' })]));
}));
$('#wxQuery').addEventListener('keydown', ev => { if (ev.key === 'Enter') $('#wxFind').click(); });
$('#wxSave').addEventListener('click', ev => guarded(ev.target, async () => {
  const on = $('#wxOn').checked;
  if (on && !WX.place) throw new Error('Search for your city and choose it first');
  const body = { weatherOn: on, wUnit: $('#wxUnit').value };
  if (WX.place) { body.wLat = WX.lat; body.wLon = WX.lon; body.wPlace = WX.place; }
  await api('/api/settings', body);
  S.cfg = await api('/api/settings');
  fillWeather();
  toast(on ? 'Saved - fetching the weather…' : 'Weather is off', 'good');
  pollStatus(); setTimeout(pollStatus, 4000);
}));
$('#wxRefresh').addEventListener('click', ev => guarded(ev.target, async () => {
  await api('/api/weather/refresh', {});
  toast('Refreshing…'); setTimeout(pollStatus, 4000);
}));
function wxStatusText(st) {
  const w = st.wx || {};
  if (w.state === 'ok') {
    const ago = st.now && w.fetched ? Math.max(0, Math.round((st.now - w.fetched) / 60)) : null;
    return `${w.temp}°${w.unit} ${w.words}, high ${w.hi}° low ${w.lo}°` + (ago == null ? '' : `, updated ${ago} min ago`);
  }
  if (w.state === 'error') return 'Last fetch failed' + (w.err ? ': ' + w.err : '') + '. The Logs tab has the history.';
  if (w.state === 'pending') return st.timeSynced ? 'Fetching…' : "Waiting for the buddy's clock to sync (it needs internet access).";
  return 'Weather is off.';
}

// ------------------------------------------------------------------ WiFi networks card (Settings tab)
const WF = { nets: [], max: 5, scan: {} };
const utf8Len = t => new TextEncoder().encode(t).length;
async function loadWifi() {
  const j = await api('/api/wifi');
  WF.nets = j.nets || []; WF.max = j.max || 5;
  renderWifi();
}
function renderWifi() {
  const cur = (S.status && S.status.ssid) || '';
  const key = cur + '|' + JSON.stringify(WF.nets);
  if (key === WF.key) return;                     // the status poll runs every 5 s: do not rebuild (and eat clicks) for nothing
  WF.key = key;
  const ul = $('#wifiList');
  if (!WF.nets.length) { ul.replaceChildren(h('li', { class: 'muted', text: 'No network saved yet.' })); return; }
  ul.replaceChildren(...WF.nets.map((n, i) => h('li', { class: 'net' },
    h('span', { class: 'netname', text: `${i + 1}. ${n.ssid}` }),
    n.ssid === cur ? pill('connected', 'ok') : null,
    n.hasPass ? null : pill('open', ''),
    h('button', { class: 'ghost', type: 'button', title: 'Try this network earlier', 'aria-label': 'Move up', text: '▲', disabled: i === 0, onclick: ev => wifiMove(ev.target, n, -1) }),
    h('button', { class: 'ghost', type: 'button', title: 'Try this network later', 'aria-label': 'Move down', text: '▼', disabled: i === WF.nets.length - 1, onclick: ev => wifiMove(ev.target, n, +1) }),
    n.ssid === cur ? null : h('button', { class: 'ghost', type: 'button', text: 'Connect now', onclick: ev => wifiConnect(ev.target, n) }),
    h('button', { class: 'ghost', type: 'button', text: 'Remove', onclick: ev => wifiRemove(ev.target, n) }))));
}
const wifiMove = (btn, n, dir) => guarded(btn, async () => { await api('/api/wifi/move', { ssid: n.ssid, dir }); await loadWifi(); });
const wifiRemove = (btn, n) => guarded(btn, async () => {
  const cur = S.status && S.status.ssid;
  const only = WF.nets.length === 1;
  const msg = `Remove "${n.ssid}"?` + (n.ssid === cur ? ' The buddy is using it right now and stays connected until it restarts or loses the signal.' : '')
    + (only ? ' It is the only saved network: without one, the buddy starts its setup network after the next restart.' : '');
  if (!confirm(msg)) return;
  await api('/api/wifi/remove', { ssid: n.ssid });
  await loadWifi();
  toast('Removed', 'good');
});
const wifiConnect = (btn, n) => guarded(btn, async () => {
  if (!confirm(`Switch the buddy to "${n.ssid}" now?\n\nThis page will lose its connection and the buddy may get a new address. Open http://${S.cfg.host || 'buddy'}.local, or read the address on the buddy's screen (long-press the knob). If it cannot join, it goes back to your other saved networks.`)) return;
  await api('/api/wifi/connect', { ssid: n.ssid });
  toast(`Switching to ${n.ssid}…`);
  setTimeout(() => { pollStatus(); loadWifi().catch(() => {}); }, 15000);
});
const signalWord = r => r > -60 ? 'strong' : r > -75 ? 'ok' : 'weak';
$('#wfScan').addEventListener('click', ev => guarded(ev.target, async () => {
  toast('Scanning… the buddy is busy for a few seconds');
  const list = await api('/api/scan?detail=1');
  WF.scan = {};
  for (const ap of list) WF.scan[ap.ssid] = ap;
  $('#wfScanList').replaceChildren(...(list.length
    ? list.map(ap => h('li', {}, h('button', { class: 'ghost', type: 'button', text: `${ap.ssid}  –  ${signalWord(ap.rssi)}, ${ap.open ? 'open' : 'password'}`,
        onclick: () => { $('#wfName').value = ap.ssid; $('#wfPass').value = ''; $('#wfScanList').replaceChildren(); $('#wfPass').focus(); } })))
    : [h('li', { class: 'muted', text: 'No networks found. The buddy only sees 2.4 GHz networks.' })]));
}));
$('#wfAdd').addEventListener('click', ev => guarded(ev.target, async () => {
  const ssid = $('#wfName').value, pass = $('#wfPass').value;
  if (!ssid || utf8Len(ssid) > 32) throw new Error('The network name must be 1 to 32 characters');
  if (pass && (utf8Len(pass) < 8 || utf8Len(pass) > 63)) throw new Error('A WiFi password is 8 to 63 characters (or empty for an open network)');
  const known = WF.nets.find(n => n.ssid === ssid);
  if (!known && WF.nets.length >= WF.max) throw new Error(`The buddy remembers up to ${WF.max} networks: remove one first`);
  if (!known && !pass && WF.scan[ssid] && !WF.scan[ssid].open) throw new Error('That network needs a password');
  const body = { ssid };
  if (pass || !known) body.pass = pass;           // an existing network keeps its password when this box is empty
  await api('/api/wifi/add', body);
  $('#wfName').value = ''; $('#wfPass').value = ''; $('#wfScanList').replaceChildren();
  await loadWifi();
  toast(known ? 'Network updated' : 'Network saved - it is at the bottom of the list', 'good');
}));

// ------------------------------------------------------------------ clock faces + photo (Settings tab)
// A face is chosen with one request and shows on the buddy at once. The photo is cropped and shrunk to
// 240x240 here, converted to the screen's own 16-bit colour (RGB565) and uploaded as a small file: the
// buddy needs no image decoder and never receives a huge or odd file. Format: src/photo.h.
const SWATCHES = ['#00D1FF', '#FF7A18', '#3DDC84', '#FF4D8D', '#FFD23F', '#A78BFA', '#FF4D4D', '#F2F4F7'];
const CROP = 240;
const PHOTO_BYTES = 8 + CROP * CROP * 2;
const PH = { img: null, w: 0, h: 0, zoom: 1, cx: 0, cy: 0, drag: null, fresh: false };   // fresh: a file was chosen (not the buddy's own photo)

function faceName(id) { const f = (S.cfg.faces || []).find(x => x.id === id); return f ? f.name : 'Clock'; }
function photoFaceId() { const f = (S.cfg.faces || []).find(x => x.key === 'photo'); return f ? f.id : -1; }

function fillFaces() {
  const c = S.cfg;
  $('#faceGrid').replaceChildren(...(c.faces || []).map(f => h('button', {
    class: 'face' + (f.id === c.face ? ' on' : ''), type: 'button', role: 'radio', 'aria-checked': f.id === c.face ? 'true' : 'false',
    title: f.about, onclick: () => chooseFace(f.id),
  }, h('img', { src: 'faces/' + f.key + '.png', alt: '', width: 96, height: 96 }), h('span', { text: f.name }))));
  const acc = (c.accent || '#00D1FF').toUpperCase();
  $('#faceAccent').value = acc.toLowerCase();
  $('#faceSwatches').replaceChildren(...SWATCHES.map(col => h('button', {
    class: 'swatch' + (col === acc ? ' on' : ''), type: 'button', style: 'background:' + col, title: col, 'aria-label': 'Accent ' + col,
    onclick: () => saveAccent(col),
  })));
  $('#photoDim').value = c.photoDim; $('#oPhotoDim').textContent = c.photoDim;
  $('#cropDim').style.setProperty('--dim', c.photoDim / 100);
  $('#photoRemove').disabled = !c.hasPhoto;
}

async function chooseFace(id) {
  await guarded(null, async () => {
    await api('/api/settings', { face: id });
    S.cfg.face = id;
    fillFaces();
    const need = id === photoFaceId() && !S.cfg.hasPhoto;
    toast(faceName(id) + ' face selected - tap the buddy to see it' + (need ? '. Choose a photo below first.' : ''), need ? '' : 'good');
  });
}

async function saveAccent(col) {
  col = col.toUpperCase();
  await guarded(null, async () => {
    try { await api('/api/settings', { accent: col }); }
    catch (e) { fillFaces(); throw e; }                    // put the picker back to the colour the buddy really uses
    S.cfg.accent = col;
    fillFaces();
    toast('Accent colour saved', 'good');
  });
}
$('#faceAccent').addEventListener('change', e => saveAccent(e.target.value));

let dimTimer = 0;
$('#photoDim').addEventListener('input', e => {
  const v = +e.target.value;
  $('#oPhotoDim').textContent = v;
  $('#cropDim').style.setProperty('--dim', v / 100);
  clearTimeout(dimTimer);
  dimTimer = setTimeout(() => guarded(null, async () => { await api('/api/settings', { photoDim: v }); S.cfg.photoDim = v; }), 400);
});

// ---- the crop box
function cropRect() {           // the part of the picture that fills the round screen, in picture pixels
  const base = Math.max(CROP / PH.w, CROP / PH.h);          // at zoom 1 the shorter side just fills the screen
  const scale = base * PH.zoom;
  const sw = CROP / scale;
  const sx = Math.min(Math.max(PH.cx - sw / 2, 0), PH.w - sw);
  const sy = Math.min(Math.max(PH.cy - sw / 2, 0), PH.h - sw);
  return { sx, sy, sw, scale };
}
function drawCrop(ctx) {
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, CROP, CROP);
  if (!PH.img) return;
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';
  const r = cropRect();
  ctx.drawImage(PH.img, r.sx, r.sy, r.sw, r.sw, 0, 0, CROP, CROP);
}
function redrawCrop() { drawCrop($('#cropCv').getContext('2d')); }

function photoStateText(msg) {
  $('#photoState').textContent = msg != null ? msg
    : PH.fresh ? 'Drag the picture to move it, zoom to fill the circle, then press "Use this photo".'
    : S.cfg.hasPhoto ? 'This is the photo on the buddy.' : 'No photo on the buddy yet - choose one.';
}

function setPicture(img, w, h, fresh) {
  PH.img = img; PH.w = w; PH.h = h; PH.zoom = 1; PH.cx = w / 2; PH.cy = h / 2; PH.fresh = fresh;
  $('#photoZoom').value = 100; $('#photoZoom').disabled = !fresh;
  $('#photoUse').disabled = !fresh;
  redrawCrop();
  photoStateText();
}

async function openImage(file) {
  if (typeof createImageBitmap === 'function') {
    try { const b = await createImageBitmap(file, { imageOrientation: 'from-image' }); return { img: b, w: b.width, h: b.height }; }
    catch (_) { /* try the old way below */ }
  }
  const url = URL.createObjectURL(file);
  try {
    const im = new Image();
    im.src = url;
    await im.decode();
    return { img: im, w: im.naturalWidth, h: im.naturalHeight };
  } finally { URL.revokeObjectURL(url); }
}

$('#photoFile').addEventListener('change', async ev => {
  const f = ev.target.files[0];
  if (!f) return;
  try {
    if (f.size > 60e6) throw new Error('That file is very large - pick one under 60 MB');
    let { img, w, h } = await openImage(f);
    if (!w || !h) throw new Error('That picture has no size');
    const longSide = Math.max(w, h);
    if (longSide > 1400) {                    // shrink once up front: smoother final result and far less memory
      const k = 1400 / longSide, c = document.createElement('canvas');
      c.width = Math.max(1, Math.round(w * k)); c.height = Math.max(1, Math.round(h * k));
      const cx = c.getContext('2d');
      cx.imageSmoothingQuality = 'high';
      cx.drawImage(img, 0, 0, c.width, c.height);
      img = c; w = c.width; h = c.height;
    }
    setPicture(img, w, h, true);
  } catch (e) {
    toast(/size|large/.test(e.message) ? e.message : 'This browser cannot open that picture. Try a JPEG or PNG.', 'bad');
  }
});

$('#photoZoom').addEventListener('input', e => { PH.zoom = e.target.value / 100; redrawCrop(); });
(() => {                                      // drag to move, wheel to zoom
  const cv = $('#cropCv');
  cv.addEventListener('pointerdown', e => {
    if (!PH.fresh) return;
    cv.setPointerCapture(e.pointerId);
    PH.drag = { x: e.clientX, y: e.clientY, cx: PH.cx, cy: PH.cy };
    cv.style.cursor = 'grabbing';
  });
  cv.addEventListener('pointermove', e => {
    if (!PH.drag) return;
    const k = CROP / cv.getBoundingClientRect().width, r = cropRect();
    PH.cx = PH.drag.cx - (e.clientX - PH.drag.x) * k / r.scale;
    PH.cy = PH.drag.cy - (e.clientY - PH.drag.y) * k / r.scale;
    redrawCrop();
  });
  const end = () => { PH.drag = null; cv.style.cursor = ''; };
  cv.addEventListener('pointerup', end);
  cv.addEventListener('pointercancel', end);
  cv.addEventListener('wheel', e => {
    if (!PH.fresh) return;
    e.preventDefault();
    const z = $('#photoZoom');
    z.value = Math.min(400, Math.max(100, +z.value - Math.sign(e.deltaY) * 10));
    z.dispatchEvent(new Event('input'));
  }, { passive: false });
})();

// ---- RGB565 file (src/photo.h): "BPH1", 240, 240, then 240*240 little-endian pixels rrrrrggggggbbbbb
const BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
function pack565(rgba) {
  const out = new Uint8Array(PHOTO_BYTES);
  out.set([66, 80, 72, 49, CROP & 255, CROP >> 8, CROP & 255, CROP >> 8]);
  let o = 8;
  for (let y = 0; y < CROP; y++) for (let x = 0; x < CROP; x++) {
    const i = (y * CROP + x) * 4;
    const t = (BAYER4[(y & 3) * 4 + (x & 3)] + 0.5) / 16 - 0.5;         // ordered dither: smooth skies instead of bands
    const r = Math.min(31, Math.max(0, Math.floor(rgba[i] * 31 / 255 + 0.5 + t)));
    const g = Math.min(63, Math.max(0, Math.floor(rgba[i + 1] * 63 / 255 + 0.5 + t)));
    const b = Math.min(31, Math.max(0, Math.floor(rgba[i + 2] * 31 / 255 + 0.5 + t)));
    const v = (r << 11) | (g << 5) | b;
    out[o++] = v & 255; out[o++] = v >> 8;
  }
  return out;
}
function unpack565(bytes) {                   // the buddy's file -> ImageData, or null if it is not one
  if (bytes.length !== PHOTO_BYTES || String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]) !== 'BPH1') return null;
  const id = new ImageData(CROP, CROP);
  for (let n = 0; n < CROP * CROP; n++) {
    const v = bytes[8 + n * 2] | (bytes[9 + n * 2] << 8);
    id.data[n * 4] = Math.round((v >> 11) * 255 / 31);
    id.data[n * 4 + 1] = Math.round(((v >> 5) & 63) * 255 / 63);
    id.data[n * 4 + 2] = Math.round((v & 31) * 255 / 31);
    id.data[n * 4 + 3] = 255;
  }
  return id;
}

$('#photoUse').addEventListener('click', async ev => { await guarded(ev.target, async () => {
  if (!PH.img) return;
  const c = document.createElement('canvas');
  c.width = c.height = CROP;
  const cx = c.getContext('2d', { willReadFrequently: true });
  drawCrop(cx);
  const fd = new FormData();
  fd.append('photo', new Blob([pack565(cx.getImageData(0, 0, CROP, CROP).data)], { type: 'application/octet-stream' }), 'photo.bin');
  const r = await fetch('/api/photo', { method: 'POST', headers: { 'X-Buddy': '1' }, body: fd, cache: 'no-store' });
  let j = null;
  try { j = await r.json(); } catch (_) { /* not JSON */ }
  if (!r.ok) throw new Error((j && j.error) || `HTTP ${r.status}`);
  S.cfg = await api('/api/settings');
  fillFaces();
  await showDevicePhoto();
  $('#photoFile').value = '';
  toast('Photo saved' + (S.cfg.face === photoFaceId() ? ' - tap the buddy to see it' : ' - pick the Photo face to show it'), 'good');
}); $('#photoUse').disabled = !PH.fresh; });

$('#photoRemove').addEventListener('click', async ev => { await guarded(ev.target, async () => {
  if (!confirm('Remove the photo from the buddy?')) return;
  await api('/api/photo/delete', {});
  S.cfg = await api('/api/settings');
  fillFaces();
  setPicture(null, 1, 1, false);
  $('#photoFile').value = '';
  toast('Photo removed', 'good');
}); $('#photoRemove').disabled = !S.cfg.hasPhoto; });

// show the photo that is on the buddy right now in the crop box
async function showDevicePhoto() {
  if (!S.cfg.hasPhoto) { setPicture(null, 1, 1, false); return; }
  const r = await fetch('/api/photo', { cache: 'no-store' });
  if (!r.ok) { setPicture(null, 1, 1, false); return; }
  const id = unpack565(new Uint8Array(await r.arrayBuffer()));
  if (!id) { setPicture(null, 1, 1, false); return; }
  const c = document.createElement('canvas');
  c.width = c.height = CROP;
  c.getContext('2d').putImageData(id, 0, 0);
  setPicture(c, CROP, CROP, false);
}

function fillSettings() {
  const c = S.cfg;
  $('#sBright').value = c.bright; $('#oBright').textContent = c.bright;
  $('#sNight').value = c.nightBright; $('#oNight').textContent = c.nightBright;
  $('#sFrom').value = c.sleepFrom; $('#sTo').value = c.sleepTo;
  $('#sLead').value = c.leadMin; $('#sClock').value = c.clockSecs;
  $('#sLed').checked = c.ledOn; $('#sBle').checked = c.bleHid;
  $('#sTz').value = c.tz; $('#sTzPick').value = TZ.some(t => t[1] === c.tz) ? c.tz : '';
  $('#sHost').value = c.host;
  $('#helperHost').value = c.helperHost || ''; $('#helperPort').value = c.helperPort;
  fillWeather();
  fillFaces();
  $('#calUrl').placeholder = c.calSet ? 'saved (hidden) - paste a new URL to replace it' : 'https://script.google.com/macros/s/…/exec?t=…';
}

$('#settingsSave').addEventListener('click', ev => guarded(ev.target, async () => {
  const body = {
    bright: +$('#sBright').value, nightBright: +$('#sNight').value, sleepFrom: +$('#sFrom').value, sleepTo: +$('#sTo').value,
    leadMin: +$('#sLead').value, clockSecs: +$('#sClock').value, ledOn: $('#sLed').checked, bleHid: $('#sBle').checked,
    tz: $('#sTz').value.trim(), host: $('#sHost').value.trim(),
  };
  const pw = $('#sPass').value;
  if (pw) body.newAdmin = pw;
  const r = await api('/api/settings', body);
  $('#sPass').value = '';
  S.cfg = await api('/api/settings');
  toast(r.rebootNeeded ? 'Saved - restart the buddy to apply the name / Bluetooth change' : 'Saved', 'good');
  pollStatus();
}));
$('#reboot').addEventListener('click', ev => guarded(ev.target, async () => {
  if (!confirm('Restart the buddy now?')) return;
  await api('/api/reboot', {});
  toast('Restarting…');
  waitForDevice();
}));

async function waitForDevice() {
  for (let i = 0; i < 40; i++) {
    await new Promise(r => setTimeout(r, 1500));
    try { await api('/api/status'); location.reload(); return; } catch (_) { /* still booting */ }
  }
  toast('The buddy did not come back - check the screen', 'bad');
}

// ------------------------------------------------------------------ firmware tab
$('#fwGo').addEventListener('click', ev => guarded(ev.target, async () => {
  const f = $('#fwFile').files[0];
  if (!f) return toast('Choose a firmware.bin first', 'bad');
  const magic = new Uint8Array(await f.slice(0, 1).arrayBuffer())[0];
  if (magic !== 0xE9 || f.size < 100000) return toast('That does not look like an ESP32 firmware image', 'bad');
  if (!confirm(`Install ${f.name} (${Math.round(f.size / 1024)} KB)? The buddy restarts afterwards.`)) return;
  const bar = $('#fwBar');
  bar.hidden = false; bar.value = 0;
  await new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.open('POST', '/update');
    x.setRequestHeader('X-Buddy', '1');
    x.upload.onprogress = e => { if (e.lengthComputable) bar.value = e.loaded / e.total * 100; };
    x.onload = () => {
      if (x.status === 200) return resolve();
      let msg = 'HTTP ' + x.status;
      try { msg = JSON.parse(x.responseText).error || msg; } catch (_) { /* keep */ }
      reject(new Error(msg));
    };
    x.onerror = () => reject(new Error('connection lost during upload'));
    const fd = new FormData();
    fd.append('firmware', f, f.name);
    x.send(fd);
  });
  toast('Installed - restarting…', 'good');
  waitForDevice();
}));

// ------------------------------------------------------------------ logs tab
// Log lines can contain text that came from outside (for example a message from the calendar script),
// so they are only ever added with textContent, never as HTML.
let logText = '', logTimer = null;
const LOG_BAD = /fail|error|lost|refus|crash|brownout|not connected|cannot|could not|wrong|locked|rate-limit/i;
const pad2 = n => String(n).padStart(2, '0');
function logTime(l) {
  if (l.e) { const d = new Date(l.e * 1000); return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`; }
  return `+${pad2(Math.floor(l.u / 3600))}:${pad2(Math.floor(l.u % 3600 / 60))}:${pad2(l.u % 60)}`;   // time since the buddy started
}
async function loadLogs() {
  const box = $('#logOut');
  const j = await api('/api/logs');
  const lines = j.lines || [];
  const stick = box.scrollHeight - box.scrollTop - box.clientHeight < 40;       // keep following the end unless the user scrolled up
  logText = lines.map(l => logTime(l) + '  ' + l.m).join('\n');
  box.replaceChildren(...(lines.length
    ? lines.map(l => h('div', { class: 'logline' + (LOG_BAD.test(l.m) ? ' bad' : '') }, h('time', { text: logTime(l) }), h('span', { text: l.m })))
    : [h('div', { class: 'muted', text: 'Nothing logged yet.' })]));
  if (stick) box.scrollTop = box.scrollHeight;
}
function copyText(text) {
  if (navigator.clipboard && window.isSecureContext) return navigator.clipboard.writeText(text);
  const ta = h('textarea', { style: 'position:fixed;left:-9999px;top:0' });      // http://buddy.local is not a secure context
  ta.value = text; document.body.append(ta); ta.select();
  const ok = document.execCommand('copy');
  ta.remove();
  return ok ? Promise.resolve() : Promise.reject(new Error('Could not copy - select the log and copy it by hand'));
}
function logsVisible(on) {
  clearInterval(logTimer); logTimer = null;
  if (!on) return;
  guarded(null, loadLogs);
  logTimer = setInterval(() => { if (!document.hidden && $('#logAuto').checked) loadLogs().catch(() => { /* next tick */ }); }, 3000);
}
$('#logRefresh').addEventListener('click', ev => guarded(ev.target, loadLogs));
$('#logCopy').addEventListener('click', ev => guarded(ev.target, async () => { await copyText(logText); toast('Copied', 'good'); }));

// ------------------------------------------------------------------ tabs + boot
function showTab(name) {
  $$('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.tab === name));
  $$('.tab').forEach(t => t.classList.toggle('on', t.id === 'tab-' + name));
  logsVisible(name === 'logs');
  try { history.replaceState(null, '', '#' + name); } catch (_) { /* ignore */ }
}
$$('#tabs button').forEach(b => b.addEventListener('click', () => showTab(b.dataset.tab)));

(async function boot() {
  fillTz();
  const first = location.hash.slice(1);
  if ($('#tab-' + first)) showTab(first);
  try {
    [S.status, S.cfg] = await Promise.all([api('/api/status'), api('/api/settings')]);
    fillSettings();
    showDevicePhoto().catch(() => { /* the crop box just stays empty */ });
    renderStatus();
    loadWifi().catch(e => toast('WiFi list: ' + e.message, 'bad'));
    await loadLayout();
  } catch (e) {
    toast('Cannot reach the buddy: ' + e.message, 'bad');
  }
  setInterval(() => { if (!document.hidden) pollStatus(); }, 5000);
})();
