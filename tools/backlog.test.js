'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const B = require('./backlog.js');

const data = B.load();

test('the shipped backlog is consistent (ids, labels, milestones, files exist)', () => {
  assert.deepEqual(B.check(data), []);
  assert.ok(data.items.length >= 20);
});

test('check() catches broken entries', () => {
  const bad = JSON.parse(JSON.stringify(data));
  bad.items[0].files = ['does/not/exist.txt'];
  bad.items[1].labels = ['nonsense'];
  bad.items[2].id = bad.items[3].id;
  bad.items[4].milestone = 'nope';
  bad.items[5].acceptance = [];
  const errs = B.check(bad).join('\n');
  assert.match(errs, /file not found: does\/not\/exist\.txt/);
  assert.match(errs, /unknown label "nonsense"/);
  assert.match(errs, /duplicate id/);
  assert.match(errs, /unknown milestone "nope"/);
  assert.match(errs, /acceptance/);
  const planned = JSON.parse(JSON.stringify(data));
  planned.items[0].files = ['+brand/new/file.txt'];
  assert.deepEqual(B.check(planned), []);
});

test('markdown contains every item, its files and checklist', () => {
  const md = B.markdown(data);
  for (const it of data.items) {
    assert.ok(md.includes(`### ${it.id} - ${it.title}`), it.id);
    assert.ok(md.includes(`- [ ] ${it.acceptance[0]}`), it.id);
  }
  for (const ms of Object.keys(data.milestones)) assert.ok(md.includes(`## ${ms}`));
});

function fakeExec(responses = {}) {
  const calls = [];
  const fn = (cmd, args, opts) => {
    calls.push({ cmd, args, input: opts && opts.input });
    const key = args.slice(0, 2).join(' ');
    if (key === 'issue create') return `https://github.com/me/desk-buddy/issues/${calls.length}\n`;
    return responses[key] || '';
  };
  fn.calls = calls;
  return fn;
}

test('dry run prints commands and never calls gh', () => {
  const exec = fakeExec();
  const lines = [];
  const r = B.planIssues(data, { create: false, exec, log: m => lines.push(m) });
  assert.equal(exec.calls.length, 0);
  assert.equal(r.made, data.items.length);
  assert.ok(lines.some(l => l.includes('gh issue create --title') && l.includes('[M0-01]')));
  assert.ok(lines.some(l => l.includes('gh label create')));
});

test('create mode: labels, milestones, issues, project add, and skips existing', () => {
  const firstTitle = `[${data.items[0].id}] ${data.items[0].title}`;
  const firstMilestone = Object.keys(data.milestones)[0];
  const exec = fakeExec({
    'issue list': `${firstTitle}\nsome unrelated issue\n`,
    'api repos/me/desk-buddy/milestones?state=all&per_page=100': `${firstMilestone}\n`,
  });
  const r = B.planIssues(data, { create: true, repo: 'me/desk-buddy', project: '3', owner: 'me', exec, log: () => {} });
  const by = prefix => exec.calls.filter(c => c.args.slice(0, prefix.length).join(' ') === prefix.join(' '));

  assert.equal(by(['auth', 'status']).length, 1);
  assert.equal(by(['label', 'create']).length, Object.keys(data.labels).length);
  assert.ok(by(['label', 'create']).every(c => c.args.includes('--force') && c.args.includes('--repo')));

  const msCreates = exec.calls.filter(c => c.args[0] === 'api' && c.args.some(a => a.startsWith('title=')));
  assert.equal(msCreates.length, Object.keys(data.milestones).length - 1, 'the existing milestone is not re-created');
  assert.ok(msCreates.every(c => c.args[1] === 'repos/me/desk-buddy/milestones'));

  const issues = by(['issue', 'create']);
  assert.equal(issues.length, data.items.length - 1, 'the issue that already exists is skipped');
  assert.equal(r.skipped, 1);
  assert.ok(!issues.some(c => c.args.includes(firstTitle)));
  const m11 = issues.find(c => c.args.includes('[M0-07] Mac helper: install and test end to end'));
  assert.ok(m11, 'M0-07 created');
  assert.deepEqual(m11.args.slice(0, 3), ['issue', 'create', '--title']);
  assert.ok(m11.args.includes('--body-file') && m11.args[m11.args.indexOf('--body-file') + 1] === '-');
  assert.ok(m11.args.includes('--milestone'));
  assert.match(m11.input, /\*\*Files\*\*/);
  assert.match(m11.input, /- \[ \] `node buddy-helper\.js --init`/);
  assert.match(m11.input, /Backlog id M0-07/);

  const adds = by(['project', 'item-add']);
  assert.equal(adds.length, issues.length);
  assert.deepEqual(adds[0].args.slice(0, 5), ['project', 'item-add', '3', '--owner', 'me']);
  assert.match(adds[0].args[6], /^https:\/\/github\.com\/me\/desk-buddy\/issues\/\d+$/);
});

test('no shell is involved: odd characters travel as plain arguments', () => {
  const odd = JSON.parse(JSON.stringify(data));
  odd.items[0].title = 'Quote " and $(touch pwned) and `ticks`';
  const exec = fakeExec();
  B.planIssues(odd, { create: true, exec, log: () => {} });
  const c = exec.calls.find(x => x.args[0] === 'issue' && x.args[1] === 'create');
  assert.equal(c.cmd, 'gh');
  assert.ok(c.args.includes(`[${odd.items[0].id}] ${odd.items[0].title}`));
});
