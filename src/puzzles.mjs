// Where every dive's seven prompts come from.
//   daily / archive : the stored puzzle for that New York date (Groq-generated or fallback)
//   unlimited       : seven prompts drawn from the classic pool plus past generated days
//   pack            : a fixed chapter from src/content/packs.mjs
import { CLASSIC } from './content/classic.mjs';
import { PACKS, packById } from './content/packs.mjs';
import { etDate } from '../public/rules.mjs';

export const DAY_MS = 86400000;
export const launchDate = env => /^\d{4}-\d{2}-\d{2}$/.test(env?.LAUNCH_DATE || '') ? env.LAUNCH_DATE : '2026-11-24';
export const dayNumber = (env, date) => Math.floor((Date.parse(date) - Date.parse(launchDate(env))) / DAY_MS) + 1;
export const addDays = (date, n) => new Date(Date.parse(date) + n * DAY_MS).toISOString().slice(0, 10);

// Deterministic shuffle so a fallback day is the same no matter when it is first requested.
function seeded(seed) { let s = seed >>> 0 || 1; return () => ((s = Math.imul(s ^ (s >>> 15), 2246822507) ^ Math.imul(s ^ (s >>> 13), 3266489909)) >>> 0) / 4294967296; }
function shuffle(list, seed) { const out = [...list], r = seeded(seed); for (let i = out.length - 1; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [out[i], out[j]] = [out[j], out[i]]; } return out; }
export function fallbackDay(env, date) {
  const index = Math.max(0, dayNumber(env, date) - 1), perCycle = Math.floor(CLASSIC.length / 7), cycle = Math.floor(index / perCycle), slot = index % perCycle;
  const prompts = shuffle(CLASSIC, 7919 * (cycle + 1)).slice(slot * 7, slot * 7 + 7);
  return { date, dayNumber: dayNumber(env, date), source: 'SourceKrillion classic pool', prompts };
}

const cache = new Map();
export async function storedPuzzle(env, date) {
  if (cache.has(date)) return cache.get(date);
  const row = await env.DB.prepare('SELECT content FROM puzzles WHERE date=?').bind(date).first();
  if (!row) return null;
  const p = JSON.parse(row.content); cache.set(date, p); return p;
}
export async function savePuzzle(env, puzzle, replace = false) {
  await env.DB.prepare(`INSERT INTO puzzles(date,day_number,source,content) VALUES(?,?,?,?) ${replace ? 'ON CONFLICT(date) DO UPDATE SET day_number=excluded.day_number,source=excluded.source,content=excluded.content,updated_at=CURRENT_TIMESTAMP' : 'ON CONFLICT(date) DO NOTHING'}`)
    .bind(puzzle.date, puzzle.dayNumber, puzzle.source, JSON.stringify(puzzle)).run();
  cache.delete(puzzle.date);
  return storedPuzzle(env, puzzle.date);
}
export const clearPuzzleCache = () => cache.clear();

// The daily puzzle for a date. A date that has been served once is frozen, so everyone
// (and the archive later) sees the same seven prompts.
export async function dailyPuzzle(env, date = etDate()) {
  const stored = await storedPuzzle(env, date);
  if (stored) return stored;
  return savePuzzle(env, fallbackDay(env, date));
}

// Prompts from generated days before today; they join the Unlimited pool once their day has passed.
export async function generatedPool(env, before = etDate()) {
  const rows = (await env.DB.prepare("SELECT content FROM puzzles WHERE date<? AND source LIKE 'Generated%' ORDER BY date DESC LIMIT 120").bind(before).all()).results;
  return rows.flatMap(r => JSON.parse(r.content).prompts);
}
export async function unlimitedPool(env) { const extra = await generatedPool(env); const ids = new Set(CLASSIC.map(p => p.id)); return [...CLASSIC, ...extra.filter(p => !ids.has(p.id))]; }
export async function pickUnlimited(env, recentIds = []) {
  const pool = await unlimitedPool(env), recent = new Set(recentIds);
  const fresh = pool.filter(p => !recent.has(p.id)), source = fresh.length >= 7 ? fresh : pool;
  return shuffle(source, (Math.random() * 2 ** 31) | 0).slice(0, 7);
}

export function packChapter(packId, index) {
  const pack = packById(packId), chapter = pack?.chapters[index];
  return chapter ? { pack, chapter, prompts: chapter.prompts } : null;
}
export { PACKS, CLASSIC };
