#!/usr/bin/env node
'use strict';
/*
 * Backlog tooling. tools/backlog.json is the single source of truth.
 *
 *   node tools/backlog.js --check                  validate the JSON and that the listed files exist
 *   node tools/backlog.js --markdown               regenerate docs/BACKLOG.md
 *   node tools/backlog.js --issues                 DRY RUN: print the GitHub CLI commands that would run
 *   node tools/backlog.js --issues --create        create labels, milestones and issues with `gh`
 *        [--repo OWNER/REPO]                       default: the repository of the current folder
 *        [--project N --owner LOGIN]               also add every new issue to GitHub Project number N
 *
 * Re-running --create is safe: issues whose "[ID]" prefix already exists are skipped.
 * Nothing here pushes code. It never runs unless you pass --create.
 */
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const DATA = path.join(__dirname, 'backlog.json');

function load(file = DATA) { return JSON.parse(fs.readFileSync(file, 'utf8')); }

function check(b, root = ROOT) {
  const errors = [];
  const ids = new Set();
  for (const it of b.items) {
    const at = `${it.id || '(no id)'}: `;
    if (!/^M\d-\d\d$/.test(it.id || '')) errors.push(at + 'id must look like M0-01');
    if (ids.has(it.id)) errors.push(at + 'duplicate id');
    ids.add(it.id);
    if (!it.title) errors.push(at + 'missing title');
    if (!b.milestones[it.milestone]) errors.push(at + `unknown milestone "${it.milestone}"`);
    for (const l of it.labels || []) if (!b.labels[l]) errors.push(at + `unknown label "${l}"`);
    if (!it.body) errors.push(at + 'missing body');
    if (!Array.isArray(it.acceptance) || !it.acceptance.length) errors.push(at + 'needs at least one acceptance line');
    if (!Array.isArray(it.files) || !it.files.length) errors.push(at + 'needs a file list');
    for (const f of it.files || []) {
      const isNew = f.startsWith('+');
      const rel = isNew ? f.slice(1) : f;
      if (!isNew && !fs.existsSync(path.join(root, rel))) errors.push(at + `file not found: ${rel} (prefix with + if it is to be created)`);
    }
  }
  return errors;
}

const fileLabel = f => (f.startsWith('+') ? `\`${f.slice(1)}\` *(new)*` : `\`${f}\``);
const issueTitle = it => `[${it.id}] ${it.title}`;

function issueBody(it) {
  return [
    it.body, '',
    '**Files**', ...it.files.map(f => `- ${fileLabel(f)}`), '',
    '**Done when**', ...it.acceptance.map(a => `- [ ] ${a}`), '',
    `<sub>Backlog id ${it.id}, from tools/backlog.json</sub>`, '',
  ].join('\n');
}

function markdown(b) {
  const out = [];
  out.push('# Backlog', '',
    '> Generated from `tools/backlog.json` by `node tools/backlog.js --markdown`. Edit the JSON, not this file.', '',
    'Each item is written as a GitHub issue: what to do, which files it touches, and a "done when" checklist.',
    'Create them all (and the labels and milestones) with `node tools/backlog.js --issues --create` after the repository exists;',
    'run it without `--create` first to see the commands. Then add the issues to your GitHub Project.', '',
    'Nothing in this repository has run on a real board yet, so milestone M0 is the real first step.', '');
  out.push('| ID | Title | Labels |', '|---|---|---|');
  for (const it of b.items) out.push(`| ${it.id} | ${it.title} | ${(it.labels || []).join(', ')} |`);
  out.push('');
  for (const [ms, desc] of Object.entries(b.milestones)) {
    out.push(`## ${ms}`, '', desc, '');
    for (const it of b.items.filter(i => i.milestone === ms)) {
      out.push(`### ${it.id} - ${it.title}`, '', `Labels: ${(it.labels || []).map(l => `\`${l}\``).join(' ')}`, '', it.body, '',
        'Files:', ...it.files.map(f => `- ${fileLabel(f)}`), '', 'Done when:', ...it.acceptance.map(a => `- [ ] ${a}`), '');
    }
  }
  return out.join('\n');
}

// ------------------------------------------------------------------ GitHub CLI plumbing
const q = s => (/^[\w@%+=:,./-]+$/.test(s) ? s : `'${String(s).replace(/'/g, `'\\''`)}'`);

function makeGh({ create, repo, exec = execFileSync, log = console.log }) {
  return function gh(args, { input, readOnly = false } = {}) {
    const shown = `gh ${args.map(q).join(' ')}`;
    if (!create && !readOnly) { log(`  ${shown}${input ? '   (body from stdin)' : ''}`); return ''; }
    if (!create && readOnly) return null;            // dry run: pretend nothing exists yet
    return exec('gh', args, { input, encoding: 'utf8', stdio: ['pipe', 'pipe', 'inherit'] }).toString();
  };
}

function planIssues(b, { create, repo, project, owner, exec, log = console.log }) {
  const gh = makeGh({ create, repo, exec, log });
  const R = repo ? ['--repo', repo] : [];
  const apiBase = repo ? `repos/${repo}` : 'repos/{owner}/{repo}';

  if (create) gh(['auth', 'status'], { readOnly: true });

  log('# labels');
  for (const [name, color] of Object.entries(b.labels)) gh(['label', 'create', name, '--color', color, '--force', ...R]);

  log('# milestones');
  const have = gh(['api', `${apiBase}/milestones?state=all&per_page=100`, '--jq', '.[].title'], { readOnly: true });
  const haveSet = new Set((have || '').split('\n').filter(Boolean));
  for (const [title, description] of Object.entries(b.milestones)) {
    if (haveSet.has(title)) { log(`  (milestone exists: ${title})`); continue; }
    gh(['api', `${apiBase}/milestones`, '-f', `title=${title}`, '-f', `description=${description}`]);
  }

  log('# issues');
  const existing = gh(['issue', 'list', '--state', 'all', '--limit', '500', '--json', 'title', '--jq', '.[].title', ...R], { readOnly: true });
  const existingSet = new Set((existing || '').split('\n').filter(Boolean));
  let made = 0, skipped = 0;
  for (const it of b.items) {
    const title = issueTitle(it);
    if ([...existingSet].some(t => t.startsWith(`[${it.id}]`))) { skipped++; log(`  (exists: ${title})`); continue; }
    const args = ['issue', 'create', '--title', title, '--body-file', '-', '--milestone', it.milestone, ...R];
    for (const l of it.labels || []) args.push('--label', l);
    const url = (gh(args, { input: issueBody(it) }) || '').trim();
    made++;
    if (project && owner) gh(['project', 'item-add', String(project), '--owner', owner, '--url', url || '<issue-url>']);
  }
  log(`${create ? 'created' : 'would create'} ${made} issue(s), ${skipped} already existed`);
  return { made, skipped };
}

// ------------------------------------------------------------------ main
function main(argv) {
  const has = f => argv.includes(f);
  const val = f => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : undefined; };
  const b = load();
  const errors = check(b);
  if (errors.length) { console.error('backlog.json problems:\n  - ' + errors.join('\n  - ')); process.exit(1); }

  if (has('--check')) return console.log(`backlog OK: ${b.items.length} items, ${Object.keys(b.milestones).length} milestones`);
  if (has('--markdown')) {
    fs.mkdirSync(path.join(ROOT, 'docs'), { recursive: true });
    fs.writeFileSync(path.join(ROOT, 'docs', 'BACKLOG.md'), markdown(b));
    return console.log('wrote docs/BACKLOG.md');
  }
  if (has('--issues')) {
    if ((val('--project') && !val('--owner')) || (!val('--project') && val('--owner'))) { console.error('--project and --owner go together'); process.exit(1); }
    if (!has('--create')) console.log('DRY RUN - nothing is created. Add --create to run these.\n');
    return planIssues(b, { create: has('--create'), repo: val('--repo'), project: val('--project'), owner: val('--owner') });
  }
  console.log(fs.readFileSync(__filename, 'utf8').split('*/')[0].replace(/^#!.*\n/, '').replace(/^\/\*\n?/, '').replace(/^'use strict';\n/, ''));
}

module.exports = { check, markdown, issueBody, issueTitle, planIssues, load };
if (require.main === module) main(process.argv.slice(2));
