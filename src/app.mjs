// SourceKrillion request handler. Pure fetch(Request) -> Response so it runs the same
// in tests and inside the Docker server (src/server.mjs).
import { SITE } from './config.mjs';
import { homePage, infoPage } from './html.mjs';
import { TIERS, normalize, matchAnswer, etDate, resetAt, ROUND_MS, PREVIEW_MS, ROUNDS } from '../public/rules.mjs';
import { dailyPuzzle, storedPuzzle, pickUnlimited, packChapter, unlimitedPool, launchDate, dayNumber, addDays, PACKS } from './puzzles.mjs';
import { ensureGenerated } from './generate.mjs';

const COOKIE = 'sk_player';
const json = (data, status = 200, extra = {}) => Response.json(data, { status, headers: { 'Cache-Control': 'no-store', ...extra } });
const fail = (message, status = 400) => json({ error: message }, status);
export const playerFrom = request => request.headers.get('Cookie')?.match(new RegExp(`(?:^|;\\s*)${COOKIE}=([a-f0-9-]{36})`))?.[1] || null;
const cookieFor = (id, url) => `${COOKIE}=${id}; Path=/; HttpOnly; SameSite=Lax; Max-Age=31536000${url.protocol === 'https:' ? '; Secure' : ''}`;
const sha256 = async text => [...new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text)))].map(x => x.toString(16).padStart(2, '0')).join('');
const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
const randomCode = n => Array.from(crypto.getRandomValues(new Uint8Array(n)), b => CODE_CHARS[b % CODE_CHARS.length]).join('');
function headers(response, additional = {}) {
  const h = new Headers(response.headers);
  for (const [k, v] of Object.entries({ 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'strict-origin-when-cross-origin', 'Permissions-Policy': 'camera=(), microphone=(), geolocation=()', 'X-Frame-Options': 'SAMEORIGIN', ...additional })) h.set(k, v);
  return new Response(response.body, { status: response.status, headers: h });
}

// ---------- players ----------
async function ensurePlayer(env, id) {
  const row = await env.DB.prepare('SELECT * FROM players WHERE id=?').bind(id).first();
  if (row) return row;
  for (let i = 0; i < 5; i++) {
    const code = randomCode(6);
    const r = await env.DB.prepare('INSERT OR IGNORE INTO players(id,friend_code,created_at) VALUES(?,?,?)').bind(id, code, Date.now()).run();
    if (r.meta.changes) break;
  }
  return env.DB.prepare('SELECT * FROM players WHERE id=?').bind(id).first();
}
const meDTO = p => ({ name: p.name, friendCode: p.friend_code, hasRestoreCode: !!p.restore_hash });

// ---------- puzzles for runs ----------
async function puzzleForRun(env, run) {
  if (run.content) return JSON.parse(run.content);
  if (run.mode === 'pack') {
    const [, packId, index] = run.puzzle.split(':'), found = packChapter(packId, Number(index));
    if (!found) throw new Error('This chapter no longer exists.');
    return { date: run.date, dayNumber: Number(index) + 1, prompts: found.prompts };
  }
  return (await storedPuzzle(env, run.puzzle)) || dailyPuzzle(env, run.puzzle);
}
function runMeta(env, run, puzzle) {
  if (run.mode === 'pack') {
    const [, packId, index] = run.puzzle.split(':'), { pack, chapter } = packChapter(packId, Number(index));
    const unit = pack.unit[0].toUpperCase() + pack.unit.slice(1);
    return { title: `${pack.name} · ${unit} ${Number(index) + 1}`, kicker: chapter.title.toUpperCase(), pack: { id: pack.id, name: pack.name, chapter: Number(index), chapters: pack.chapters.length, chapterTitle: chapter.title }, shareLabel: `${pack.name} ${Number(index) + 1}` };
  }
  if (run.mode === 'unlimited' || run.mode === 'practice') return { title: `∞ Dive #${run.number || 1} complete`.replace(' complete', ''), kicker: 'THE ENDLESS DIVE', shareLabel: `∞ ${run.number || 1}` };
  if (run.mode === 'archive') return { title: `Archive · Dive #${puzzle.dayNumber}`, kicker: 'FROM THE ARCHIVE', shareLabel: `#${puzzle.dayNumber} (archive)` };
  return { title: `Dive #${puzzle.dayNumber}`, kicker: 'THE DAILY DIVE', shareLabel: `#${puzzle.dayNumber}` };
}
function promptDTO(env, puzzle) {
  return { date: puzzle.date, dayNumber: puzzle.dayNumber, resetsAt: resetAt(etDate()), generated: /^Generated/.test(puzzle.source || ''), prompts: puzzle.prompts.map(({ id, text }) => ({ id, text })) };
}
async function state(env, run) {
  const results = (await env.DB.prepare('SELECT round,raw_answer AS rawAnswer,answer,tier,score FROM answers WHERE run_id=? ORDER BY round').bind(run.id).all()).results;
  const puzzle = await puzzleForRun(env, run);
  return { ...promptDTO(env, puzzle), ...runMeta(env, run, puzzle), runId: run.id, mode: run.mode === 'practice' ? 'unlimited' : run.mode, number: run.number, round: run.next_round, roundStart: run.round_start, pausedAt: run.paused_at || null, serverNow: Date.now(), completed: !!run.completed, results, score: results.reduce((s, r) => s + r.score, 0) };
}
async function ownRun(env, id, player) {
  if (!/^[a-f0-9-]{36}$/.test(id || '') || !player) return null;
  return env.DB.prepare('SELECT * FROM runs WHERE id=? AND player=?').bind(id, player).first();
}
async function limit(env, request, player) {
  const window = Math.floor(Date.now() / 60000);
  const key = await sha256(`${player || ''}|${request.headers.get('X-Forwarded-For')?.split(',')[0] || request.headers.get('CF-Connecting-IP') || 'local'}|${window}`);
  const count = await env.DB.prepare('INSERT INTO rate_limits(key,window,count) VALUES(?,?,1) ON CONFLICT(key,window) DO UPDATE SET count=count+1 RETURNING count').bind(key, window).first();
  if (count.count === 1) await env.DB.prepare('DELETE FROM rate_limits WHERE window<?').bind(window - 5).run();
  return count.count <= Number(env.RATE_LIMIT || 120);
}

// ---------- logbook / streaks ----------
async function completedRuns(env, player) {
  const rows = (await env.DB.prepare("SELECT r.id,r.mode,r.puzzle,r.date,r.number,r.created_at AS createdAt,COALESCE(SUM(a.score),0) AS score,GROUP_CONCAT(COALESCE(a.tier,'miss'),',') AS tiers FROM runs r LEFT JOIN answers a ON a.run_id=r.id WHERE r.player=? AND r.completed=1 GROUP BY r.id ORDER BY r.created_at DESC").bind(player).all()).results;
  return rows.map(r => ({ ...r, mode: r.mode === 'practice' ? 'unlimited' : r.mode, tiers: (r.tiers || '').split(',').filter(Boolean) }));
}
export function streaks(dates, today = etDate()) {
  const days = [...new Set(dates)].sort();
  let best = 0, run = 0, last = null;
  for (const d of days) { run = last && Date.parse(d) - Date.parse(last) === 86400000 ? run + 1 : 1; best = Math.max(best, run); last = d; }
  const gap = last ? (Date.parse(today) - Date.parse(last)) / 86400000 : Infinity;
  return { current: gap <= 1 ? run : 0, best };
}
async function logbook(env, player) {
  const runs = player ? await completedRuns(env, player) : [];
  const daily = runs.filter(r => r.mode === 'daily'), s = streaks(daily.map(r => r.date));
  return {
    stats: { current: s.current, bestStreak: s.best, played: daily.length, best: Math.max(0, ...daily.map(r => r.score)), average: daily.length ? Math.round(daily.reduce((t, r) => t + r.score, 0) / daily.length) : 0, unlimited: runs.filter(r => r.mode === 'unlimited').length, archive: runs.filter(r => r.mode === 'archive').length, packs: runs.filter(r => r.mode === 'pack').length },
    runs: runs.slice(0, 200).map(r => ({ id: r.id, mode: r.mode, date: r.date, puzzle: r.puzzle, number: r.number, score: r.score, tiers: r.tiers, dayNumber: r.mode === 'daily' || r.mode === 'archive' ? dayNumber(env, r.puzzle) : null })),
  };
}

// ---------- start a dive ----------
async function startRun(env, player, data) {
  const today = etDate();
  let mode = data.mode || (data.practice === true ? 'unlimited' : 'daily');
  if (!['daily', 'archive', 'unlimited', 'pack'].includes(mode)) mode = 'daily';
  const insert = async (puzzle, date, extra = {}) => {
    const id = crypto.randomUUID();
    await env.DB.prepare('INSERT OR IGNORE INTO runs(id,player,date,mode,puzzle,content,number,round_start,created_at) VALUES(?,?,?,?,?,?,?,?,?)').bind(id, player, date, mode, puzzle, extra.content || null, extra.number || null, Date.now() + PREVIEW_MS, Date.now()).run();
    return id;
  };
  if (mode === 'daily') {
    await dailyPuzzle(env, today);
    let run = await env.DB.prepare("SELECT * FROM runs WHERE player=? AND date=? AND mode='daily'").bind(player, today).first();
    if (!run) { await insert(today, today); run = await env.DB.prepare("SELECT * FROM runs WHERE player=? AND date=? AND mode='daily'").bind(player, today).first(); }
    return run;
  }
  if (mode === 'archive') {
    const date = String(data.date || '');
    if (!/^\d{4}-\d{2}-\d{2}$/.test(date) || date < launchDate(env) || date >= today) throw Object.assign(new Error('That day is not in the archive.'), { status: 404 });
    await dailyPuzzle(env, date);
    let run = await env.DB.prepare("SELECT * FROM runs WHERE player=? AND puzzle=? AND mode='archive'").bind(player, date).first();
    if (!run) { await insert(date, today); run = await env.DB.prepare("SELECT * FROM runs WHERE player=? AND puzzle=? AND mode='archive'").bind(player, date).first(); }
    return run;
  }
  if (mode === 'pack') {
    const found = packChapter(String(data.pack || ''), Number(data.chapter));
    if (!found) throw Object.assign(new Error('Pack chapter not found.'), { status: 404 });
    const progress = await packProgress(env, player);
    const p = progress.find(x => x.id === found.pack.id);
    if (!p.chapters[Number(data.chapter)].unlocked) throw Object.assign(new Error('Finish the previous chapter first.'), { status: 403 });
    const key = `pack:${found.pack.id}:${Number(data.chapter)}`;
    const open = await env.DB.prepare("SELECT * FROM runs WHERE player=? AND puzzle=? AND mode='pack' AND completed=0 ORDER BY created_at DESC").bind(player, key).first();
    if (open) return open;
    return ownRun(env, await insert(key, today), player);
  }
  // unlimited: resume an unfinished dive, otherwise draw seven fresh prompts
  const open = await env.DB.prepare("SELECT * FROM runs WHERE player=? AND mode IN ('unlimited','practice') AND completed=0 ORDER BY created_at DESC").bind(player).first();
  if (open && data.fresh !== true) return open;
  if (open) await env.DB.prepare('UPDATE runs SET completed=1 WHERE id=?').bind(open.id).run();
  const recent = (await env.DB.prepare("SELECT content FROM runs WHERE player=? AND mode='unlimited' AND content IS NOT NULL ORDER BY created_at DESC LIMIT 6").bind(player).all()).results.flatMap(r => JSON.parse(r.content).prompts.map(p => p.id));
  const prompts = await pickUnlimited(env, recent);
  const number = (await env.DB.prepare("SELECT COUNT(*) AS n FROM runs WHERE player=? AND mode IN ('unlimited','practice')").bind(player).first()).n + 1;
  return ownRun(env, await insert(`unlimited:${number}`, today, { content: JSON.stringify({ date: today, dayNumber: number, prompts }), number }), player);
}

async function packProgress(env, player) {
  const rows = player ? (await env.DB.prepare("SELECT r.puzzle,MAX(t.score) AS best FROM runs r JOIN (SELECT run_id,SUM(score) AS score FROM answers GROUP BY run_id) t ON t.run_id=r.id WHERE r.player=? AND r.mode='pack' AND r.completed=1 GROUP BY r.puzzle").bind(player).all()).results : [];
  const best = new Map(rows.map(r => [r.puzzle, r.best]));
  return PACKS.map(pack => {
    let unlocked = true;
    const chapters = pack.chapters.map((c, i) => {
      const b = best.get(`pack:${pack.id}:${i}`), out = { index: i, title: c.title, unlocked, completed: b !== undefined, best: b ?? null };
      unlocked = b !== undefined; return out;
    });
    return { id: pack.id, name: pack.name, tagline: pack.tagline, cta: pack.cta, unit: pack.unit, chapters, completed: chapters.filter(c => c.completed).length };
  });
}

async function friendsView(env, player) {
  const me = await ensurePlayer(env, player), today = etDate();
  const rows = (await env.DB.prepare('SELECT p.id,p.name,p.friend_code FROM friends f JOIN players p ON p.id=f.friend WHERE f.player=? ORDER BY f.created_at').bind(player).all()).results;
  const scoreFor = async id => (await env.DB.prepare("SELECT r.completed,COALESCE(SUM(a.score),0) AS score,GROUP_CONCAT(COALESCE(a.tier,'miss'),',') AS tiers FROM runs r LEFT JOIN answers a ON a.run_id=r.id WHERE r.player=? AND r.date=? AND r.mode='daily' GROUP BY r.id").bind(id, today).first());
  const mine = await scoreFor(player), revealed = !!mine?.completed;
  const people = [];
  for (const p of [{ id: me.id, name: me.name, friend_code: me.friend_code, you: true }, ...rows]) {
    const s = await scoreFor(p.id), dates = (await env.DB.prepare("SELECT date FROM runs WHERE player=? AND mode='daily' AND completed=1").bind(p.id).all()).results.map(r => r.date);
    people.push({ name: p.name || 'Unnamed diver', code: p.friend_code, you: !!p.you, played: !!s?.completed, score: s?.completed && (revealed || p.you) ? s.score : null, tiers: s?.completed && (revealed || p.you) ? (s.tiers || '').split(',') : null, streak: streaks(dates, today).current });
  }
  people.sort((a, b) => (b.score ?? -1) - (a.score ?? -1) || a.name.localeCompare(b.name));
  return { date: today, dayNumber: dayNumber(env, today), revealed, me: meDTO(me), people };
}

async function api(request, env, url, ctx) {
  const path = url.pathname, method = request.method;
  let player = playerFrom(request), setCookie = null;
  const newPlayer = () => { if (!player) { player = crypto.randomUUID(); setCookie = cookieFor(player, url); } return player; };
  const reply = (data, status = 200) => json(data, status, setCookie ? { 'Set-Cookie': setCookie } : {});

  if (method === 'GET') {
    if (path === '/api/today') { const p = await dailyPuzzle(env, etDate()); return json({ ...promptDTO(env, p), title: `Dive #${p.dayNumber}` }); }
    if (path === '/api/health') return json({ ok: true, name: SITE.name, database: !!(await env.DB.prepare('SELECT 1 AS ok').first()), generator: env.GROQ_API_KEY ? 'groq' : 'off' });
    if (path === '/api/state') { const run = await ownRun(env, url.searchParams.get('run'), player); return run ? json(await state(env, run)) : fail('Dive not found on this device.', 404); }
    if (path === '/api/review') {
      const run = await ownRun(env, url.searchParams.get('run'), player);
      if (!run?.completed) return fail('Finish your dive before opening the answer sheet.', 403);
      const puzzle = await puzzleForRun(env, run), own = await state(env, run);
      let others = [];
      if (run.mode === 'daily' || run.mode === 'archive') others = (await env.DB.prepare("SELECT r.id,SUM(a.score) AS score FROM runs r JOIN answers a ON a.run_id=r.id WHERE r.puzzle=? AND r.mode='daily' AND r.completed=1 AND r.id<>? GROUP BY r.id").bind(run.puzzle, run.id).all()).results;
      const dist = Array.from({ length: 71 }, () => 0); others.forEach(p => dist[Math.min(70, Math.floor(p.score / 10))]++);
      return json({ prompts: puzzle.prompts.map(p => ({ ...p, answers: p.answers.map(a => ({ ...a, score: TIERS[a.tier].score })).sort((a, b) => b.score - a.score || a.answer.localeCompare(b.answer)) })), population: others.length, betterThan: others.length >= 5 ? Math.round(others.filter(p => p.score < own.score).length / others.length * 100) : null, dist });
    }
    if (path === '/api/me') return player ? json(meDTO(await ensurePlayer(env, player))) : json({ name: '', friendCode: null, hasRestoreCode: false });
    if (path === '/api/logbook') return json(await logbook(env, player));
    if (path === '/api/archive') {
      const today = etDate(), start = launchDate(env), days = [];
      const played = player ? new Map((await env.DB.prepare("SELECT r.puzzle,r.mode,r.completed,COALESCE(SUM(a.score),0) AS score FROM runs r LEFT JOIN answers a ON a.run_id=r.id WHERE r.player=? AND r.mode IN ('daily','archive') GROUP BY r.id").bind(player).all()).results.map(r => [r.puzzle, r])) : new Map();
      for (let d = addDays(today, -1); d >= start && days.length < 400; d = addDays(d, -1)) { const p = played.get(d); days.push({ date: d, dayNumber: dayNumber(env, d), played: !!p, completed: !!p?.completed, score: p?.completed ? p.score : null, mode: p?.mode || null }); }
      return json({ today, days });
    }
    if (path === '/api/packs') return json({ packs: await packProgress(env, player) });
    if (path === '/api/friends') { if (!player) return json({ revealed: false, me: null, people: [] }); return json(await friendsView(env, player)); }
    if (path === '/api/party/rooms') return json({ rooms: env.PARTY?.publicRooms() || [], enabled: !!env.PARTY });
    if (path === '/api/bundle') {
      // Full answer lists for the native app's offline mode.
      const today = await dailyPuzzle(env, etDate()), tomorrow = await storedPuzzle(env, addDays(etDate(), 1));
      return json({ version: 1, generatedAt: Date.now(), today, pool: await unlimitedPool(env), packs: PACKS.map(p => ({ id: p.id, name: p.name, tagline: p.tagline, cta: p.cta, unit: p.unit, chapters: p.chapters })), ...(tomorrow && env.BUNDLE_TOMORROW === 'true' ? { tomorrow } : {}) }, 200, { 'Cache-Control': 'no-store' });
    }
    return fail('Unknown endpoint.', 404);
  }

  if (method !== 'POST') return fail('Unknown endpoint.', 404);
  if (request.headers.get('Origin') && request.headers.get('Origin') !== url.origin && !(env.ALLOWED_ORIGINS || '').split(',').includes(request.headers.get('Origin'))) return fail('Cross-origin requests are not accepted.', 403);
  if (Number(request.headers.get('Content-Length') || 0) > 8000) return fail('Request is too large.', 413);
  if (!await limit(env, request, player)) return fail('Too many requests. Take a breath and try again.', 429);
  let data; try { const raw = await request.text(); if (raw.length > 8000) return fail('Request is too large.', 413); data = raw ? JSON.parse(raw) : {}; } catch { return fail('Invalid request.'); }
  if (!data || typeof data !== 'object' || Array.isArray(data)) return fail('Invalid request.');

  if (path === '/api/start') {
    newPlayer(); await ensurePlayer(env, player);
    try { return reply(await state(env, await startRun(env, player, data))); } catch (e) { return reply({ error: e.message }, e.status || 400); }
  }
  if (path === '/api/pause') {
    const run = await ownRun(env, data.runId, player); if (!run) return fail('Dive not found.', 404);
    if (data.paused === true && !run.completed && run.round_start !== null && !run.paused_at && Date.now() <= run.round_start + ROUND_MS) await env.DB.prepare('UPDATE runs SET paused_at=? WHERE id=? AND paused_at IS NULL').bind(Date.now(), run.id).run();
    else if (data.paused === false && run.paused_at) await env.DB.prepare('UPDATE runs SET round_start=round_start+?-paused_at,paused_at=NULL WHERE id=? AND paused_at IS NOT NULL').bind(Date.now(), run.id).run();
    return json(await state(env, await ownRun(env, run.id, player)));
  }
  if (path === '/api/next') {
    const run = await ownRun(env, data.runId, player); if (!run) return fail('Dive not found.', 404);
    if (run.paused_at) return fail('Close the menu to resume your dive.', 409);
    if (!run.completed && run.round_start === null) await env.DB.prepare('UPDATE runs SET round_start=? WHERE id=? AND round_start IS NULL AND completed=0').bind(Date.now() + PREVIEW_MS, run.id).run();
    return json(await state(env, await ownRun(env, run.id, player)));
  }
  if (path === '/api/answer') {
    const run = await ownRun(env, data.runId, player); if (!run) return fail('Dive not found.', 404);
    if (run.paused_at) return fail('Close the menu to resume your dive.', 409);
    if (run.completed || run.round_start === null || data.round !== run.next_round) return json({ state: await state(env, run), alreadyScored: true });
    if (Date.now() < run.round_start && env.LOCAL_DEVELOPMENT !== 'true') return fail('The answer window has not opened yet.', 409);
    const puzzle = await puzzleForRun(env, run), prompt = puzzle.prompts[run.next_round];
    const raw = typeof data.answer === 'string' ? data.answer.trim().slice(0, 256) : '';
    const late = Date.now() > run.round_start + ROUND_MS;
    const timely = !late || (data.timeout === true && Date.now() <= run.round_start + ROUND_MS + 1500);
    // DEEP_WORD lives only in the server environment: typing it always lands on the trench floor.
    const deep = env.DEEP_WORD && raw && timely && normalize(raw) && normalize(raw) === normalize(env.DEEP_WORD);
    const match = deep ? { answer: raw, tier: 'krillion', correction: false } : raw && timely ? matchAnswer(raw, prompt.answers) : null;
    if (!match && raw && !late && !data.timeout) return json({ rejected: true, message: `“${raw}”: no echo. try again` });
    if (match?.correction && data.confirm !== true && !late && !data.timeout && data.previewCorrection !== false) return json({ correction: match.answer, original: raw });
    const result = { round: run.next_round, rawAnswer: raw || null, answer: match?.answer || null, tier: match?.tier || null, score: match ? TIERS[match.tier].score : 0, quip: match?.quip || null, promptText: prompt.text };
    await env.DB.batch([
      env.DB.prepare('INSERT OR IGNORE INTO answers(run_id,round,raw_answer,answer,tier,score,created_at) SELECT id,?,?,?,?,?,? FROM runs WHERE id=? AND next_round=? AND round_start IS NOT NULL AND completed=0').bind(result.round, result.rawAnswer, result.answer, result.tier, result.score, Date.now(), run.id, result.round),
      env.DB.prepare('UPDATE runs SET next_round=?,round_start=NULL,completed=? WHERE id=? AND next_round=? AND EXISTS(SELECT 1 FROM answers WHERE run_id=? AND round=?)').bind(result.round + 1, result.round === ROUNDS - 1 ? 1 : 0, run.id, result.round, run.id, result.round),
    ]);
    const fresh = await state(env, await ownRun(env, run.id, player));
    return json({ result: { ...result, ...fresh.results.find(r => r.round === result.round) }, state: fresh });
  }
  if (path === '/api/feedback') {
    if (!player) return fail('Play a dive first.', 403);
    const text = String(data.text || '').trim().slice(0, 1000), kind = data.kind === 'suggestion' ? 'suggestion' : 'rating';
    if (!text) return fail('Please enter a message.');
    if ((await env.DB.prepare('SELECT COUNT(*) AS n FROM feedback WHERE player=? AND date=?').bind(player, etDate()).first()).n >= 3) return fail('Thanks — enough feedback for today.', 429);
    await env.DB.prepare('INSERT INTO feedback(player,date,kind,text,created_at) VALUES(?,?,?,?,?)').bind(player, etDate(), kind, text, Date.now()).run();
    return json({ ok: true });
  }
  if (path === '/api/me') {
    newPlayer(); await ensurePlayer(env, player);
    const name = String(data.name ?? '').replace(/[\u0000-\u001f<>]/g, '').trim().slice(0, 18);
    await env.DB.prepare('UPDATE players SET name=? WHERE id=?').bind(name, player).run();
    return reply(meDTO(await ensurePlayer(env, player)));
  }
  if (path === '/api/restore-code') {
    newPlayer(); await ensurePlayer(env, player);
    const code = `${randomCode(4)}-${randomCode(4)}-${randomCode(4)}`;
    await env.DB.prepare('UPDATE players SET restore_hash=? WHERE id=?').bind(await sha256(code.replace(/-/g, '')), player).run();
    return reply({ code });
  }
  if (path === '/api/restore') {
    const code = String(data.code || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
    if (code.length !== 12) return fail('Restore codes have 12 letters and numbers.');
    const found = await env.DB.prepare('SELECT * FROM players WHERE restore_hash=?').bind(await sha256(code)).first();
    if (!found) return fail('That restore code did not match a logbook.', 404);
    return json({ ok: true, me: meDTO(found) }, 200, { 'Set-Cookie': cookieFor(found.id, url) });
  }
  if (path === '/api/friends/add' || path === '/api/friends/remove') {
    if (!player) return fail('Play a dive first.', 403);
    const me = await ensurePlayer(env, player), code = String(data.code || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
    const other = await env.DB.prepare('SELECT id FROM players WHERE friend_code=?').bind(code).first();
    if (!other) return fail('No diver has that friend code.', 404);
    if (other.id === me.id) return fail('That is your own code.');
    if (path.endsWith('add')) await env.DB.batch([env.DB.prepare('INSERT OR IGNORE INTO friends(player,friend,created_at) VALUES(?,?,?)').bind(me.id, other.id, Date.now()), env.DB.prepare('INSERT OR IGNORE INTO friends(player,friend,created_at) VALUES(?,?,?)').bind(other.id, me.id, Date.now())]);
    else await env.DB.batch([env.DB.prepare('DELETE FROM friends WHERE player=? AND friend=?').bind(me.id, other.id), env.DB.prepare('DELETE FROM friends WHERE player=? AND friend=?').bind(other.id, me.id)]);
    return json(await friendsView(env, player));
  }
  if (path === '/api/party/create') {
    if (!env.PARTY) return fail('Party rooms are not available on this server.', 503);
    newPlayer(); await ensurePlayer(env, player);
    const room = env.PARTY.create(data.public === true);
    return reply({ code: room.code });
  }
  if (path === '/api/admin/generate') {
    if (!env.ADMIN_TOKEN || request.headers.get('Authorization') !== `Bearer ${env.ADMIN_TOKEN}`) return fail('Not found.', 404);
    const date = /^\d{4}-\d{2}-\d{2}$/.test(data.date || '') ? data.date : addDays(etDate(), 1);
    return json(await ensureGenerated(env, date));
  }
  return fail('Unknown endpoint.', 404);
}

const PAGES = ['/', '/archive', '/packs', '/party', '/friends', '/account', '/unlimited'];
export default {
  async fetch(request, env, ctx = {}) {
    const url = new URL(request.url);
    try {
      if (url.pathname.startsWith('/api/')) return headers(await api(request, env, url, ctx));
      if (!['GET', 'HEAD'].includes(request.method)) return headers(fail('Method not allowed.', 405));
      const html = body => headers(new Response(request.method === 'HEAD' ? null : body, { headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-cache' } }));
      if (PAGES.includes(url.pathname)) return html(homePage(url.pathname.slice(1) || 'home', env));
      if (['/privacy', '/about', '/faq'].includes(url.pathname)) return html(infoPage(url.pathname));
      if (url.pathname === '/robots.txt') return headers(new Response('User-agent: *\nAllow: /\nDisallow: /api/\n', { headers: { 'Content-Type': 'text/plain; charset=utf-8' } }));
      const asset = await env.ASSETS.fetch(request);
      return headers(asset, asset.ok ? { 'Cache-Control': url.pathname.endsWith('.woff2') ? 'public, max-age=31536000, immutable' : 'public, max-age=300' } : {});
    } catch (e) { console.error('Request failed:', e); return headers(fail('The signal is temporarily lost. Please retry.', 503)); }
  },
};
