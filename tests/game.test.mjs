import test from 'node:test';
import assert from 'node:assert/strict';
import app from '../src/app.mjs';
import { openDatabase } from '../src/db.mjs';
import { clearPuzzleCache, fallbackDay, addDays } from '../src/puzzles.mjs';
import { cleanGenerated, ensureGenerated } from '../src/generate.mjs';
import { Room, PARTY } from '../src/party.mjs';
import { CLASSIC } from '../src/content/classic.mjs';
import { PACKS } from '../src/content/packs.mjs';
import { etDate, resetAt, normalize, matchAnswer, TIERS } from '../public/rules.mjs';

function service(extraEnv = {}) {
  clearPuzzleCache();
  const env = { DB: openDatabase(':memory:'), LOCAL_DEVELOPMENT: 'true', LAUNCH_DATE: addDays(etDate(), -10), ASSETS: { fetch: async () => new Response('not found', { status: 404 }) }, ...extraEnv };
  let cookie = '';
  return {
    env,
    setCookie: c => { cookie = c; },
    cookie: () => cookie,
    async call(path, data, extra = {}) {
      const init = data === undefined ? { headers: { Cookie: cookie, ...extra } } : { method: 'POST', body: JSON.stringify(data), headers: { 'Content-Type': 'application/json', Cookie: cookie, ...extra } };
      const r = await app.fetch(new Request('http://localhost:3051' + path, init), env, {});
      if (r.headers.get('Set-Cookie')) cookie = r.headers.get('Set-Cookie').split(';')[0];
      return { status: r.status, body: await r.json() };
    },
  };
}
const answersFor = (state, i, tier) => state.__puzzle.prompts[i].answers.find(a => a.tier === tier)?.answer;
async function puzzleOf(s, game) { return (await s.call('/api/bundle')).body; }

test('New York reset follows DST', () => {
  assert.equal(etDate(Date.parse('2026-10-17T03:59:59Z')), '2026-10-16');
  assert.equal(new Date(resetAt('2026-10-17')).toISOString(), '2026-10-18T04:00:00.000Z');
  assert.equal(new Date(resetAt('2026-12-17')).toISOString(), '2026-12-18T05:00:00.000Z');
});
test('matching keeps C, C++ and C# apart and handles accents and typos', () => {
  const langs = CLASSIC.find(p => p.id === 'c-languages-prog').answers;
  assert.equal(matchAnswer('c++', langs).answer, 'C++');
  assert.equal(matchAnswer('C#', langs).answer, 'C#');
  assert.equal(matchAnswer('c', langs).answer, 'C');
  assert.equal(normalize('São Tomé & Príncipe'), 'saotomeandprincipe');
  const elements = CLASSIC.find(p => p.id === 'c-elements').answers;
  assert.equal(matchAnswer('Yttrum', elements).correction, true);
  assert.equal(matchAnswer('zzzzzzzzzz', elements), null);
});
test('content banks are large enough and every pack chapter has seven prompts', () => {
  assert.ok(CLASSIC.length >= 40);
  assert.equal(PACKS.length, 4);
  for (const p of PACKS) for (const c of p.chapters) assert.equal(c.prompts.length, 7);
  const f = fallbackDay({ LAUNCH_DATE: '2026-11-24' }, '2026-11-24');
  assert.equal(f.prompts.length, 7); assert.equal(f.dayNumber, 1);
  assert.deepEqual(fallbackDay({ LAUNCH_DATE: '2026-11-24' }, '2026-12-01').prompts.map(p => p.id), fallbackDay({ LAUNCH_DATE: '2026-11-24' }, '2026-12-01').prompts.map(p => p.id));
});
test('daily dive: seven server-scored rounds, no duplicates, other devices locked out', async () => {
  const s = service();
  const bundle = await puzzleOf(s);
  let { body: game } = await s.call('/api/start', {});
  assert.equal(game.mode, 'daily'); assert.match(game.title, /^Dive #11$/);
  const id = game.runId;
  assert.equal((await s.call('/api/answer', { runId: id, round: 0, answer: 'zzzzzzzzzz' })).body.rejected, true);
  for (let i = 0; i < 7; i++) {
    const answer = bundle.today.prompts[i].answers.find(a => a.tier === 'krillion').answer;
    const r = await s.call('/api/answer', { runId: id, round: i, answer, confirm: true });
    assert.equal(r.body.result.score, 100); game = r.body.state;
    assert.equal((await s.call('/api/answer', { runId: id, round: i, answer })).body.alreadyScored, true);
    if (i < 6) assert.equal((await s.call('/api/next', { runId: id })).body.round, i + 1);
  }
  assert.equal(game.score, 700); assert.equal(game.completed, true);
  assert.equal((await s.call('/api/start', {})).body.runId, id);
  const other = service(); other.env.DB = s.env.DB;
  assert.equal((await other.call('/api/state?run=' + id)).status, 404);
  assert.equal((await s.call('/api/start', {}, { Origin: 'https://evil.example' })).status, 403);
  const log = (await s.call('/api/logbook')).body;
  assert.equal(log.stats.played, 1); assert.equal(log.stats.current, 1); assert.equal(log.stats.best, 700);
});
test('DEEP_WORD from the environment always scores the deepest tier, and does nothing when unset', async () => {
  const s = service({ DEEP_WORD: 'Barnacle Bob' });
  const { body: g } = await s.call('/api/start', {});
  const r = await s.call('/api/answer', { runId: g.runId, round: 0, answer: 'barnacle bob' });
  assert.equal(r.body.result.tier, 'krillion'); assert.equal(r.body.result.score, 100);
  const plain = service();
  const { body: g2 } = await plain.call('/api/start', {});
  assert.equal((await plain.call('/api/answer', { runId: g2.runId, round: 0, answer: 'barnacle bob' })).body.rejected, true);
});
test('unlimited dives are numbered, fresh, and separate from the daily', async () => {
  const s = service();
  const a = (await s.call('/api/start', { mode: 'unlimited' })).body;
  assert.equal(a.mode, 'unlimited'); assert.equal(a.number, 1); assert.match(a.title, /∞ Dive #1/);
  assert.equal((await s.call('/api/start', { mode: 'unlimited' })).body.runId, a.runId, 'resumes the unfinished dive');
  const b = (await s.call('/api/start', { mode: 'unlimited', fresh: true })).body;
  assert.notEqual(b.runId, a.runId); assert.equal(b.number, 2);
  const daily = (await s.call('/api/start', {})).body; assert.equal(daily.mode, 'daily');
});
test('archive lists past days and plays each once; today and the future are refused', async () => {
  const s = service();
  const arc = (await s.call('/api/archive')).body;
  assert.equal(arc.days.length, 10); assert.equal(arc.days.at(-1).dayNumber, 1);
  const day = arc.days[2].date;
  const run = (await s.call('/api/start', { mode: 'archive', date: day })).body;
  assert.equal(run.mode, 'archive'); assert.match(run.title, /^Archive · Dive #/);
  assert.equal((await s.call('/api/start', { mode: 'archive', date: day })).body.runId, run.runId);
  assert.equal((await s.call('/api/start', { mode: 'archive', date: etDate() })).status, 404);
});
test('pack chapters unlock in order', async () => {
  const s = service();
  let packs = (await s.call('/api/packs')).body.packs;
  assert.equal(packs[0].chapters[0].unlocked, true); assert.equal(packs[0].chapters[1].unlocked, false);
  assert.equal((await s.call('/api/start', { mode: 'pack', pack: 'lexicon', chapter: 1 })).status, 403);
  let g = (await s.call('/api/start', { mode: 'pack', pack: 'lexicon', chapter: 0 })).body;
  assert.match(g.title, /Words & Language · Chapter 1/);
  for (let i = 0; i < 7; i++) {
    const answer = PACKS[0].chapters[0].prompts[i].answers[0].answer;
    g = (await s.call('/api/answer', { runId: g.runId, round: i, answer, confirm: true })).body.state;
    if (i < 6) await s.call('/api/next', { runId: g.runId });
  }
  packs = (await s.call('/api/packs')).body.packs;
  assert.equal(packs[0].chapters[0].completed, true); assert.equal(packs[0].chapters[1].unlocked, true);
});
test('friends see each other’s daily score only after finishing their own; restore codes move a logbook', async () => {
  const a = service(), b = service(); b.env.DB = a.env.DB;
  await a.call('/api/me', { name: 'Andrew' }); await b.call('/api/me', { name: 'Bea' });
  const codeB = (await b.call('/api/me')).body.friendCode;
  const view = (await a.call('/api/friends/add', { code: codeB })).body;
  assert.equal(view.people.length, 2);
  const bundle = await puzzleOf(b);
  let g = (await b.call('/api/start', {})).body;
  for (let i = 0; i < 7; i++) { g = (await b.call('/api/answer', { runId: g.runId, round: i, answer: bundle.today.prompts[i].answers[0].answer, confirm: true })).body.state; if (i < 6) await b.call('/api/next', { runId: g.runId }); }
  let seen = (await a.call('/api/friends')).body;
  assert.equal(seen.revealed, false); assert.equal(seen.people.find(p => p.name === 'Bea').score, null); assert.equal(seen.people.find(p => p.name === 'Bea').played, true);
  assert.ok((await b.call('/api/friends')).body.people.find(p => p.name === 'Bea').score > 0);
  const { code } = (await b.call('/api/restore-code', {})).body;
  const c = service(); c.env.DB = a.env.DB;
  const restored = await c.call('/api/restore', { code: code.toLowerCase() });
  assert.equal(restored.status, 200); assert.equal(restored.body.me.name, 'Bea');
  assert.equal((await c.call('/api/logbook')).body.stats.played, 1);
  assert.equal((await c.call('/api/restore', { code: 'AAAA-BBBB-CCCC' })).status, 404);
});
test('expired windows cannot score late answers', async () => {
  const s = service(); const { body: g } = await s.call('/api/start', {});
  s.env.LOCAL_DEVELOPMENT = 'false';
  await s.env.DB.prepare('UPDATE runs SET round_start=? WHERE id=?').bind(Date.now() - 30000, g.runId).run();
  const late = await s.call('/api/answer', { runId: g.runId, round: 0, answer: 'anything', timeout: true });
  assert.equal(late.body.result.score, 0);
});
test('generated puzzles are cleaned: bad prompts dropped, duplicates removed, seven required', async () => {
  const good = i => ({ text: `Name a thing number ${i}`, answers: { plankton: ['Alpha', 'Beta/B'], schooler: ['Gamma', 'Delta', 'Epsilon', 'Zeta', 'Eta'], rare: ['Theta', 'Iota', 'Kappa', 'Lambda'], deepcut: ['Mu', 'Nu', 'Xi', 'Omicron', 'Beta'], krillion: ['Pi', 'Rho'] } });
  const raw = JSON.stringify({ prompts: [{ text: 'Bad prompt', answers: {} }, ...Array.from({ length: 8 }, (_, i) => good(i))] });
  const env = { LAUNCH_DATE: '2026-11-24' };
  const p = cleanGenerated('Sure! ' + raw + ' done', '2026-11-30', env);
  assert.equal(p.prompts.length, 7); assert.equal(p.dayNumber, 7);
  assert.equal(p.prompts[0].answers.filter(a => a.answer === 'Beta').length, 1);
  assert.throws(() => cleanGenerated(JSON.stringify({ prompts: [good(1)] }), '2026-11-30', env));
  const s = service({ GROQ_API_KEY: 'test', GENERATE_PAUSE: '0' });
  let n = 0;
  const fake = async () => new Response(JSON.stringify({ choices: [{ message: { content: JSON.stringify(good(n++)) } }] }));
  const date = addDays(etDate(), 1);
  assert.equal((await ensureGenerated(s.env, date, fake)).status, 'generated');
  assert.equal((await ensureGenerated(s.env, date, fake)).status, 'exists');
  const broken = async () => new Response('nope', { status: 500 });
  assert.equal((await ensureGenerated(s.env, addDays(date, 1), broken)).status, 'failed');
});
test('party: answers pass the puffer, repeats are refused, the holder pops, last one floating wins', () => {
  let t = 0, timers = [];
  const prompts = [{ text: 'Name a country in Europe', answers: CLASSIC.find(p => p.id === 'c-europe').answers }];
  const room = new Room({ code: 'ABCD', prompts, now: () => t, random: () => 0, schedule: (fn, ms) => { timers.push({ fn, at: t + ms }); return timers.length; }, cancel: () => {} });
  room.join('a', 'Ann'); room.join('b', 'Ben'); room.join('c', 'Cy');
  assert.throws(() => room.start('b'), /host/);
  room.start('a');
  assert.equal(room.round.holder, 'a');
  assert.throws(() => room.answer('b', 'France'), /Wait/);
  assert.equal(room.answer('a', 'France').ok, true); assert.equal(room.round.holder, 'b');
  assert.equal(room.answer('b', 'france').ok, false);
  assert.equal(room.answer('b', 'Spain').ok, true); assert.equal(room.round.holder, 'c');
  const fire = () => { const next = timers.shift(); t = next.at; next.fn(); };
  fire();
  assert.equal(room.players.find(p => p.id === 'c').alive, false);
  fire(); assert.equal(room.status, 'playing'); assert.equal(room.round.holder, 'a');
  fire(); assert.equal(room.status, 'over'); assert.equal(room.winner, 'b');
  assert.equal(room.view('a').round.fuseMs, undefined, 'fuse stays secret');
  assert.ok(PARTY.minFuseMs < PARTY.maxFuseMs);
});
