#include "check.h"
#include "keycombo.h"

static int parse(const char* s, Step* st, char* err) { return parseSequence(s, st, MAX_STEPS, err, 80); }

int main() {
  Step st[MAX_STEPS];
  char err[80];

  // plain combos
  CHECK_EQ(parse("cmd+shift+4", st, err), 1);
  CHECK_EQ(st[0].kind, ST_COMBO);
  CHECK_EQ(st[0].nkeys, 3);
  CHECK_EQ(st[0].keys[0], KC_LGUI);
  CHECK_EQ(st[0].keys[1], KC_LSHIFT);
  CHECK_EQ(st[0].keys[2], '4');

  CHECK_EQ(parse("  Ctrl + CMD + Q ", st, err), 1);               // case and spaces do not matter
  CHECK_EQ(st[0].keys[0], KC_LCTRL);
  CHECK_EQ(st[0].keys[1], KC_LGUI);
  CHECK_EQ(st[0].keys[2], 'q');

  CHECK_EQ(parse("opt+left", st, err), 1);
  CHECK_EQ(st[0].keys[0], KC_LALT);
  CHECK_EQ(st[0].keys[1], KC_LEFT);

  CHECK_EQ(parse("cmd+space", st, err), 1);
  CHECK_EQ(st[0].keys[1], ' ');
  CHECK_EQ(parse("cmd+plus", st, err), 1);
  CHECK_EQ(st[0].keys[1], '+');
  CHECK_EQ(parse("cmd+-", st, err), 1);
  CHECK_EQ(st[0].keys[1], '-');
  CHECK_EQ(parse("enter", st, err), 1);
  CHECK_EQ(st[0].keys[0], KC_RETURN);

  // function keys
  CHECK_EQ(parse("f1", st, err), 1);   CHECK_EQ(st[0].keys[0], 0xC2);
  CHECK_EQ(parse("F12", st, err), 1);  CHECK_EQ(st[0].keys[0], 0xCD);
  CHECK_EQ(parse("f13", st, err), 1);  CHECK_EQ(st[0].keys[0], 0xF0);
  CHECK_EQ(parse("f24", st, err), 1);  CHECK_EQ(st[0].keys[0], 0xFB);
  CHECK_EQ(parse("f25", st, err), 0);
  CHECK_EQ(parse("f0", st, err), 0);

  // media keys
  CHECK_EQ(parse("playpause", st, err), 1);  CHECK_EQ(st[0].kind, ST_MEDIA); CHECK_EQ(st[0].media, MEDIA_PLAY_PAUSE);
  CHECK_EQ(parse("VolUp", st, err), 1);      CHECK_EQ(st[0].media, MEDIA_VOL_UP);
  CHECK_EQ(parse("mute", st, err), 1);       CHECK_EQ(st[0].media, MEDIA_MUTE);
  CHECK_EQ(parse("next", st, err), 1);       CHECK_EQ(st[0].media, MEDIA_NEXT);

  // sequences
  CHECK_EQ(parse("cmd+space;wait:350;text:Terminal;enter", st, err), 4);
  CHECK_EQ(st[0].kind, ST_COMBO);
  CHECK_EQ(st[1].kind, ST_WAIT);  CHECK_EQ(st[1].ms, 350);
  CHECK_EQ(st[2].kind, ST_TEXT);  CHECK_STR(st[2].text, "Terminal");
  CHECK_EQ(st[3].kind, ST_COMBO); CHECK_EQ(st[3].keys[0], KC_RETURN);
  CHECK_EQ(sequenceWaitMs(st, 4), 350);

  CHECK_EQ(parse("text:hello world, again + more", st, err), 1);  // text keeps spaces, commas and plus signs
  CHECK_STR(st[0].text, "hello world, again + more");
  CHECK_EQ(parse("wait:5", st, err), 1);   CHECK_EQ(st[0].ms, 10);     // clamped
  CHECK_EQ(parse("wait:99999", st, err), 1); CHECK_EQ(st[0].ms, 1500);

  // spotlight sugar
  CHECK_EQ(parse("spot:Visual Studio Code", st, err), 5);
  CHECK_EQ(st[0].keys[0], KC_LGUI); CHECK_EQ(st[0].keys[1], ' ');
  CHECK_EQ(st[1].kind, ST_WAIT);
  CHECK_EQ(st[2].kind, ST_TEXT); CHECK_STR(st[2].text, "Visual Studio Code");
  CHECK_EQ(st[3].kind, ST_WAIT);
  CHECK_EQ(st[4].keys[0], KC_RETURN);

  // errors come with a message and write nothing useful
  CHECK_EQ(parse("", st, err), 0);               CHECK_STR(err, "empty shortcut");
  CHECK_EQ(parse(";;", st, err), 0);
  CHECK_EQ(parse("cmd+banana", st, err), 0);     CHECK_STR(err, "unknown key 'banana'");
  CHECK_EQ(parse("cmd++q", st, err), 0);         CHECK_STR(err, "empty key in combo");
  CHECK_EQ(parse("cmd+", st, err), 0);
  CHECK_EQ(parse("text:", st, err), 0);
  CHECK_EQ(parse("spot:", st, err), 0);
  CHECK_EQ(parse("a+b+c+d+e+f+g", st, err), 0);  CHECK_STR(err, "too many keys in one combo");
  CHECK_EQ(parse("a;b;c;d;e;f;g;h;i", st, err), 0);   CHECK_STR(err, "too many steps");
  CHECK_EQ(parseSequence("enter", st, MAX_STEPS, nullptr, 0), 1);     // err buffer is optional
  CHECK_EQ(parseSequence(nullptr, st, MAX_STEPS, err, 80), 0);

  // spot: needs room for its five steps
  CHECK_EQ(parseSequence("a;b;c;d;spot:X", st, 8, err, 80), 0);
  char longText[200]; memset(longText, 'x', sizeof longText); longText[199] = 0;
  CHECK_EQ(parseSequence(longText, st, MAX_STEPS, err, 80), 0);       // longer than the working buffer

  // every shortcut in the default layout parses (kept in sync by hand with layout.cpp)
  const char* defaults[] = {
    "cmd+space", "cmd+shift+4", "cmd+shift+5", "ctrl+cmd+q", "cmd+tab", "ctrl+up", "ctrl+cmd+f",
    "cmd+d", "cmd+e", "cmd+shift+a", "cmd+shift+v", "cmd+shift+m", "cmd+shift+o",
    "playpause", "next", "prev", "volup", "voldown", "mute",
    "cmd+k", "cmd+shift+p", "cmd+`", "cmd+w", "cmd+t", "cmd+r", "cmd+shift+t",
  };
  for (const char* d : defaults) {
    int n = parse(d, st, err);
    if (n == 0) printf("  default shortcut failed: %s (%s)\n", d, err);
    CHECK(n > 0);
  }

  return finish();
}
