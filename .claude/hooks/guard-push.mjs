#!/usr/bin/env node
// PreToolUse hook (fleet cloud kit): for repos WITHOUT GitHub branch protection.
// Blocks `git push` to main/master, force pushes, mass pushes and branch deletes.
// Exit 0 = allow, exit 2 = block (stderr goes to Claude). Fails open on any
// internal error, with one stderr warning line. Node stdlib only, POSIX friendly.
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';

// Same approach as the fleet lease-gate: drop heredoc bodies, blank out quoted
// strings (so `echo "git push origin main"` does not count), split on command
// separators, then peel leading sudo / env / VAR=value prefixes.
const HEREDOC = /<<-?\s*(['"]?)(\w+)\1([^\n]*)\n[\s\S]*?\n[ \t]*\2(?=\s|$)/g;
const QUOTED = /"(?:[^"\]|\[\s\S])*"|'[^']*'/g;
const SEPARATORS = /[;&|(`\n\r]/;
const PREFIX = /^(?:sudo|env|[A-Za-z_]\w*=\S*)\s+/i;
// `git push`, allowing git's global options in between (-C dir, -c k=v, --no-pager ...).
const GIT_PUSH = /^git(?:\s+(?:-[Cc]\s+\S+|--\S+))*\s+push(?:\s+(.*))?$/;
const PROTECTED = new Set(['main', 'master']);

function segments(command) {
  const flat = command.replace(HEREDOC, (_m, _q, _tag, rest) => rest).replace(QUOTED, '""');
  const out = [];
  for (const seg of flat.split(SEPARATORS)) {
    let s = seg.trim();
    while (PREFIX.test(s)) s = s.replace(PREFIX, '');
    if (s) out.push(s);
  }
  return out;
}

function currentBranch() {
  const cwd = process.env.CLAUDE_PROJECT_DIR || process.cwd();
  const r = spawnSync('git', ['rev-parse', '--abbrev-ref', 'HEAD'], { cwd, encoding: 'utf8' });
  if (r.error || r.status !== 0) throw new Error(`cannot read current branch: ${(r.stderr || (r.error && r.error.message) || '').trim()}`);
  return r.stdout.trim();
}

const bare = (ref) => ref.replace(/^\+/, '').replace(/^refs\/heads\//, '');

// Returns a reason string if this push must be blocked, else null.
function checkPush(argString) {
  const tokens = (argString || '').split(/\s+/).filter(Boolean);
  const positionals = [];
  for (const t of tokens) {
    if (t.startsWith('--')) {
      if (/^--(force[\w-]*|mirror|all|delete)(=|$)/.test(t)) return `${t.split('=')[0]} is not allowed`;
    } else if (t.startsWith('-') && t.length > 1) {
      if (/[fd]/.test(t.slice(1))) return `${t} (force or delete) is not allowed`;
    } else {
      positionals.push(t);
    }
  }
  const refspecs = positionals.slice(1); // first positional is the remote
  for (const spec of positionals) {
    if (spec.startsWith('+')) return `force refspec "${spec}" is not allowed`;
    if (spec.startsWith(':')) return `delete refspec "${spec}" is not allowed`;
  }
  const isHead = (r) => r === 'HEAD' || r === '@';
  const needBranch = refspecs.length === 0 || refspecs.some((s) => isHead(s.includes(':') ? s.split(':').pop() : s));
  for (const spec of positionals) {
    const dst = bare(spec.includes(':') ? spec.split(':').pop() : spec);
    if (PROTECTED.has(dst)) return `pushing to "${dst}" is not allowed`;
  }
  if (needBranch) {
    const branch = currentBranch();
    if (PROTECTED.has(branch)) return `this push would update "${branch}" (the current branch)`;
  }
  return null;
}

try {
  const input = JSON.parse(fs.readFileSync(0, 'utf8'));
  if (input.tool_name !== 'Bash') process.exit(0);
  const command = input.tool_input && input.tool_input.command;
  if (typeof command !== 'string' || !command) process.exit(0);
  for (const seg of segments(command)) {
    const m = seg.match(GIT_PUSH);
    if (!m) continue;
    const reason = checkPush(m[1]);
    if (reason) {
      fs.writeSync(2,
        `Push blocked by the fleet push guard: ${reason}. This repo may lack GitHub branch protection, so pushes to main/master, force-pushes and branch deletes are forbidden. Push a claude/ branch instead and open a PR. Do not try to work around this.\n`);
      process.exit(2);
    }
  }
  process.exit(0);
} catch (e) {
  try { fs.writeSync(2, `guard-push: internal error, failing open: ${e && e.message ? e.message : e}\n`); } catch { /* nothing more */ }
  process.exit(0);
}
