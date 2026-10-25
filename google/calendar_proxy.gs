/**
 * Desk Buddy - Google Calendar proxy
 *
 * Why this exists: a small ESP32 cannot sign in to Google with the Calendar permission
 * (Google's "limited input device" login does not allow it). Instead this script runs as YOU on
 * Google's servers, reads your calendar, and serves a tiny JSON list over a secret link.
 * The buddy fetches that link every few minutes.
 *
 * Setup (5 minutes) - see README.md "Google Calendar" for screenshots-free steps:
 *   1. Go to script.google.com -> New project. Paste this whole file. Name it "Desk Buddy".
 *   2. Change TOKEN below to a long random string (letters and digits, 24+ characters).
 *   3. Pick the function "authorize" in the toolbar and press Run. Approve the calendar permission once.
 *   4. Deploy -> New deployment -> type "Web app".
 *        Execute as: Me        Who has access: Anyone
 *      Copy the "Web app URL" and add  ?t=YOUR_TOKEN  at the end.
 *   5. Paste that full URL into the buddy web page (Calendar & Mac -> Google Calendar).
 *
 * Privacy: "Anyone" only means anyone who knows the long URL *including the token*; without the right
 * token the script returns an error and no calendar data. Treat the URL like a password. To revoke it,
 * change TOKEN and deploy a new version (Deploy -> Manage deployments -> edit -> New version).
 * Set HIDE_TITLES = true if you only want the buddy to know *when* you are busy, not *what* it is.
 * Events marked Private show as "Busy" unless you set SHOW_PRIVATE_TITLES = true.
 */

// ------------------------------------------------------------------ settings
var TOKEN = 'CHANGE-ME-to-a-long-random-string';

// Which calendars to include. [] = your main calendar only.
// Example: ['primary', 'colleague@example.com', 'abc123@group.calendar.google.com']
var CALENDAR_IDS = [];

var HIDE_TITLES = false;          // true: every event is shown as "Busy"
var SHOW_PRIVATE_TITLES = false;  // true: events marked Private / Confidential show their real title
                                  // (HIDE_TITLES = true still wins). Only works for calendars you own.
var LOOKAHEAD_HOURS = 36;         // how far ahead the buddy looks
var MAX_EVENTS = 8;               // the buddy shows at most this many
var SKIP_ALL_DAY = true;          // all-day events (holidays, birthdays) are not meetings
var SKIP_DECLINED = true;         // events you declined
var SKIP_ZERO_LENGTH = true;      // reminders / zero-minute blocks

// ------------------------------------------------------------------ web app entry point
function doGet(e) {
  var given = (e && e.parameter && e.parameter.t) || '';
  if (TOKEN.indexOf('CHANGE-ME') === 0) return reply_({ ok: false, error: 'set TOKEN in the script first' });
  if (!safeEqual_(given, TOKEN)) return reply_({ ok: false, error: 'unauthorized' });
  try {
    return reply_({ ok: true, events: upcoming_(new Date()) });
  } catch (err) {
    return reply_({ ok: false, error: String(err && err.message ? err.message : err).slice(0, 120) });
  }
}

/** Run this once from the editor (Run button) to grant calendar access, and to preview the result. */
function authorize() {
  var events = upcoming_(new Date());
  Logger.log(JSON.stringify(events, null, 1));
  Logger.log(events.length + ' upcoming event(s). If this looks right, deploy it as a web app.');
}

// ------------------------------------------------------------------ logic
function upcoming_(now) {
  var end = new Date(now.getTime() + LOOKAHEAD_HOURS * 3600 * 1000);
  var calendars = [];
  if (CALENDAR_IDS.length === 0) {
    calendars.push(CalendarApp.getDefaultCalendar());
  } else {
    for (var i = 0; i < CALENDAR_IDS.length; i++) {
      var c = CalendarApp.getCalendarById(CALENDAR_IDS[i]);
      if (c) calendars.push(c);
    }
  }

  var seen = {};
  var out = [];
  for (var k = 0; k < calendars.length; k++) {
    var events = calendars[k].getEvents(now, end);
    for (var j = 0; j < events.length; j++) {
      var ev = events[j];
      if (SKIP_ALL_DAY && ev.isAllDayEvent()) continue;
      if (SKIP_DECLINED && ev.getMyStatus() === CalendarApp.GuestStatus.NO) continue;

      var start = Math.floor(ev.getStartTime().getTime() / 1000);
      var stop = Math.floor(ev.getEndTime().getTime() / 1000);
      if (SKIP_ZERO_LENGTH && stop <= start) continue;
      if (stop * 1000 <= now.getTime()) continue;                      // already over

      // the same meeting can appear on several calendars you include
      var key = start + ':' + stop + ':' + ev.getTitle();
      if (seen[key]) continue;
      seen[key] = true;

      var vis = ev.getVisibility();
      var isPrivate = vis === CalendarApp.Visibility.PRIVATE || vis === CalendarApp.Visibility.CONFIDENTIAL;
      var hidden = HIDE_TITLES || (isPrivate && !SHOW_PRIVATE_TITLES);
      var title = hidden ? 'Busy' : String(ev.getTitle() || '').replace(/\s+/g, ' ').trim() || '(no title)';
      out.push({ t: title.slice(0, 40), s: start, e: stop });
    }
  }
  out.sort(function (a, b) { return a.s - b.s || a.e - b.e; });
  return out.slice(0, MAX_EVENTS);
}

function safeEqual_(a, b) {
  a = String(a); b = String(b);
  var diff = a.length ^ b.length;
  var n = Math.max(a.length, b.length);
  for (var i = 0; i < n; i++) diff |= (a.charCodeAt(i) || 0) ^ (b.charCodeAt(i) || 0);
  return diff === 0;
}

function reply_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
