#include "check.h"
#include "agenda.h"

static void add(Agenda& a, const char* title, uint32_t start, uint32_t end) {
  Event& e = a.ev[a.n++];
  memset(&e, 0, sizeof e);
  strncpy(e.title, title, TITLE_MAX - 1);
  e.start = start; e.end = end;
}

int main() {
  const uint32_t T = 1800000000;   // arbitrary "now"

  // sorting and picking the next event
  Agenda a; memset(&a, 0, sizeof a);
  add(a, "Review", T + 7200, T + 9000);
  add(a, "Standup", T + 600, T + 1500);
  add(a, "Past", T - 4000, T - 3000);
  agendaSort(a);
  CHECK_STR(a.ev[0].title, "Past");
  CHECK_STR(a.ev[1].title, "Standup");
  CHECK_STR(a.ev[2].title, "Review");
  CHECK_EQ(agendaNext(a, T), 1);                        // "Past" is over
  CHECK_EQ(agendaNext(a, T + 700), 1);                  // Standup in progress still counts as next
  CHECK_EQ(agendaNext(a, T + 1500), 2);
  CHECK_EQ(agendaNext(a, T + 9000), -1);
  Agenda empty; memset(&empty, 0, sizeof empty);
  CHECK_EQ(agendaNext(empty, T), -1);

  // relative text
  char b[24];
  fmtUntil(b, sizeof b, 0);        CHECK_STR(b, "now");
  fmtUntil(b, sizeof b, -5);       CHECK_STR(b, "now");
  fmtUntil(b, sizeof b, 1);        CHECK_STR(b, "in 1m");
  fmtUntil(b, sizeof b, 60);       CHECK_STR(b, "in 1m");
  fmtUntil(b, sizeof b, 61);       CHECK_STR(b, "in 2m");
  fmtUntil(b, sizeof b, 720);      CHECK_STR(b, "in 12m");
  fmtUntil(b, sizeof b, 3599);     CHECK_STR(b, "in 60m");
  fmtUntil(b, sizeof b, 3900);     CHECK_STR(b, "in 1h 05m");
  fmtUntil(b, sizeof b, 7200);     CHECK_STR(b, "in 2h");
  fmtUntil(b, sizeof b, 3 * 86400 + 5); CHECK_STR(b, "in 3d");
  fmtLeft(b, sizeof b, 30);        CHECK_STR(b, "ending");
  fmtLeft(b, sizeof b, 600);       CHECK_STR(b, "10m left");
  fmtLeft(b, sizeof b, 5400);      CHECK_STR(b, "1h 30m left");

  // alerts: one "soon" and one "start" per event, in order, never repeated
  Agenda m; memset(&m, 0, sizeof m);
  add(m, "Standup", T + 1200, T + 2100);
  add(m, "Review", T + 5000, T + 6000);
  AlertTracker tr;
  int idx = -1;
  CHECK_EQ(agendaAlert(m, T, 600, tr, &idx), AL_NONE);               // 20 min away, lead is 10
  CHECK_EQ(agendaAlert(m, T + 599, 600, tr, &idx), AL_NONE);         // 10m01s away
  CHECK_EQ(agendaAlert(m, T + 600, 600, tr, &idx), AL_SOON);         // exactly 10 min away
  CHECK_EQ(idx, 0);
  CHECK_EQ(agendaAlert(m, T + 601, 600, tr, &idx), AL_NONE);         // no repeat
  CHECK_EQ(agendaAlert(m, T + 1199, 600, tr, &idx), AL_NONE);
  CHECK_EQ(agendaAlert(m, T + 1200, 600, tr, &idx), AL_START);
  CHECK_EQ(idx, 0);
  CHECK_EQ(agendaAlert(m, T + 1230, 600, tr, &idx), AL_NONE);        // no repeat
  CHECK_EQ(agendaAlert(m, T + 4400, 600, tr, &idx), AL_SOON);        // Review warns 10 min before
  CHECK_EQ(idx, 1);

  // joining late: an event already underway never alerts
  AlertTracker late;
  CHECK_EQ(agendaAlert(m, T + 1260, 600, late, &idx), AL_NONE);      // 60 s after the start
  CHECK_EQ(agendaAlert(m, T + 1500, 600, late, &idx), AL_NONE);

  // booted 3 minutes before a meeting: warn once, then announce the start
  AlertTracker boot;
  CHECK_EQ(agendaAlert(m, T + 1020, 600, boot, &idx), AL_SOON);
  CHECK_EQ(agendaAlert(m, T + 1021, 600, boot, &idx), AL_NONE);
  CHECK_EQ(agendaAlert(m, T + 1200, 600, boot, &idx), AL_START);

  // zero lead disables the early warning
  AlertTracker z;
  CHECK_EQ(agendaAlert(m, T + 1199, 0, z, &idx), AL_NONE);
  CHECK_EQ(agendaAlert(m, T + 1200, 0, z, &idx), AL_START);

  // titles: ASCII only, multi-byte characters collapse to one '?'
  char t[TITLE_MAX];
  agendaSanitize("Standup", t, sizeof t);                       CHECK_STR(t, "Standup");
  agendaSanitize("Caf\xC3\xA9 sync", t, sizeof t);              CHECK_STR(t, "Caf? sync");
  agendaSanitize("\xF0\x9F\x8E\x82 Party", t, sizeof t);         CHECK_STR(t, "? Party");
  agendaSanitize("tab\there\n", t, sizeof t);                  CHECK_STR(t, "tabhere");
  agendaSanitize("0123456789012345678901234567890123", t, sizeof t);
  CHECK_EQ(strlen(t), TITLE_MAX - 1);                           // truncated, always terminated
  agendaSanitize("", t, sizeof t);                              CHECK_STR(t, "");

  return finish();
}
