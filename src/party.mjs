// Puffer Party-style multiplayer: name an answer, pass the puffer before it pops.
// A room is a pure state machine; the server wires it to WebSockets.
import { matchAnswer, normalize, TIERS } from '../public/rules.mjs';

export const PARTY = { maxPlayers: 8, minFuseMs: 14000, maxFuseMs: 32000, interRoundMs: 4000, emptyRoomMs: 90000, minAnswers: 30 };
const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ';
export const roomCode = () => Array.from({ length: 4 }, () => CODE_CHARS[Math.floor(Math.random() * CODE_CHARS.length)]).join('');

export class Room {
  constructor({ code, isPublic, prompts, now = Date.now, random = Math.random, schedule = (fn, ms) => setTimeout(fn, ms), cancel = t => clearTimeout(t), onChange = () => {} }) {
    Object.assign(this, { code, isPublic: !!isPublic, prompts, now, random, schedule, cancel, onChange });
    this.players = []; this.status = 'lobby'; this.round = null; this.roundNumber = 0; this.winner = null; this.timer = null; this.log = []; this.emptySince = now();
  }
  get host() { return this.players.find(p => p.connected)?.id || this.players[0]?.id || null; }
  alive() { return this.players.filter(p => p.alive); }
  join(id, name) {
    let p = this.players.find(x => x.id === id);
    if (!p) {
      if (this.players.length >= PARTY.maxPlayers) throw new Error('This room is full.');
      p = { id, name: '', alive: this.status !== 'playing', connected: true, pops: 0, answers: 0, joinedAt: this.now() };
      this.players.push(p);
    }
    p.name = String(name || '').trim().slice(0, 18) || `Diver ${this.players.indexOf(p) + 1}`;
    p.connected = true; this.emptySince = null;
    this.changed(); return p;
  }
  leave(id) {
    const p = this.players.find(x => x.id === id); if (!p) return;
    p.connected = false;
    if (this.status !== 'playing') this.players = this.players.filter(x => x !== p);
    else if (p.alive && this.round?.holder === id) this.passFrom(id);
    if (!this.players.some(x => x.connected)) this.emptySince = this.now();
    this.changed();
  }
  start(by) {
    if (by !== this.host) throw new Error('Only the host can start.');
    if (this.status === 'playing') throw new Error('Already playing.');
    if (this.players.filter(p => p.connected).length < 2) throw new Error('Need at least two divers.');
    this.players = this.players.filter(p => p.connected);
    this.players.forEach(p => { p.alive = true; p.pops = 0; p.answers = 0; });
    this.status = 'playing'; this.winner = null; this.roundNumber = 0; this.log = [];
    this.nextRound(this.players[Math.floor(this.random() * this.players.length)].id);
  }
  nextRound(holder) {
    const prompt = this.prompts[Math.floor(this.random() * this.prompts.length)];
    const fuse = PARTY.minFuseMs + this.random() * (PARTY.maxFuseMs - PARTY.minFuseMs);
    this.roundNumber++;
    this.round = { prompt, holder, used: [], startedAt: this.now(), fuseMs: fuse, popped: null };
    this.cancel(this.timer); this.timer = this.schedule(() => this.pop(), fuse);
    this.changed();
  }
  answer(id, text) {
    if (this.status !== 'playing' || !this.round || this.round.popped) throw new Error('No puffer in play.');
    if (this.round.holder !== id) throw new Error('Wait for the puffer.');
    const raw = String(text || '').trim().slice(0, 120);
    const match = raw ? matchAnswer(raw, this.round.prompt.answers) : null;
    if (!match) return { ok: false, message: `“${raw}”: no echo. try again` };
    if (this.round.used.some(u => normalize(u.answer) === normalize(match.answer))) return { ok: false, message: `“${match.answer}” is already taken` };
    this.round.used.push({ answer: match.answer, tier: match.tier, by: id });
    this.players.find(p => p.id === id).answers++;
    this.passFrom(id);
    return { ok: true, answer: match.answer, tier: match.tier, score: TIERS[match.tier].score };
  }
  passFrom(id) {
    const alive = this.alive(); if (alive.length < 2) return;
    const i = alive.findIndex(p => p.id === id);
    let next = alive[(i + 1) % alive.length];
    for (let k = 1; k <= alive.length && !next.connected; k++) next = alive[(i + 1 + k) % alive.length];
    this.round.holder = next.id; this.round.passedAt = this.now();
    this.changed();
  }
  pop() {
    if (this.status !== 'playing' || !this.round || this.round.popped) return;
    const loser = this.players.find(p => p.id === this.round.holder);
    loser.alive = false; loser.pops++; this.round.popped = loser.id;
    this.log.push({ round: this.roundNumber, prompt: this.round.prompt.text, popped: loser.id, used: this.round.used.length });
    const alive = this.alive();
    if (alive.length <= 1) { this.status = 'over'; this.winner = alive[0]?.id || null; this.changed(); return; }
    this.changed();
    const order = this.players, start = order.indexOf(loser);
    let next = null;
    for (let k = 1; k <= order.length; k++) { const p = order[(start + k) % order.length]; if (p.alive) { next = p; break; } }
    this.timer = this.schedule(() => this.nextRound(next.id), PARTY.interRoundMs);
  }
  reset(by) {
    if (by !== this.host) throw new Error('Only the host can do that.');
    this.cancel(this.timer); this.status = 'lobby'; this.round = null; this.winner = null;
    this.players = this.players.filter(p => p.connected); this.players.forEach(p => { p.alive = true; });
    this.changed();
  }
  changed() { this.onChange(this); }
  // What clients may see. The fuse length stays secret; only how long the puffer has been inflating.
  view(forId = null) {
    const r = this.round;
    return {
      code: this.code, public: this.isPublic, status: this.status, host: this.host, you: forId, winner: this.winner, roundNumber: this.roundNumber, serverNow: this.now(),
      players: this.players.map(p => ({ id: p.id, name: p.name, alive: p.alive, connected: p.connected, answers: p.answers })),
      round: r ? { prompt: r.prompt.text, holder: r.holder, startedAt: r.startedAt, popped: r.popped, maxFuseMs: PARTY.maxFuseMs, minFuseMs: PARTY.minFuseMs, used: r.used.map(u => ({ answer: u.answer, tier: u.tier, by: u.by })) } : null,
    };
  }
  summary() { return { code: this.code, players: this.players.filter(p => p.connected).length, max: PARTY.maxPlayers, status: this.status, prompt: this.status === 'playing' ? this.round?.prompt.text : null }; }
}

export class PartyHall {
  constructor({ prompts, now, random, schedule, cancel } = {}) {
    this.prompts = prompts.filter(p => p.answers.length >= PARTY.minAnswers);
    this.options = { now, random, schedule, cancel };
    this.rooms = new Map(); this.listeners = new Map();
  }
  create(isPublic) {
    let code; do code = roomCode(); while (this.rooms.has(code));
    const room = new Room({ code, isPublic, prompts: this.prompts, ...Object.fromEntries(Object.entries(this.options).filter(([, v]) => v)), onChange: r => this.broadcast(r) });
    this.rooms.set(code, room); return room;
  }
  get(code) { return this.rooms.get(String(code || '').toUpperCase()) || null; }
  publicRooms() { return [...this.rooms.values()].filter(r => r.isPublic && r.players.some(p => p.connected)).map(r => r.summary()); }
  subscribe(code, id, send) { const set = this.listeners.get(code) || new Map(); set.set(id, send); this.listeners.set(code, set); }
  unsubscribe(code, id) { this.listeners.get(code)?.delete(id); }
  broadcast(room) { for (const [id, send] of this.listeners.get(room.code) || []) send({ t: 'state', room: room.view(id) }); }
  sweep(now = Date.now()) { for (const [code, r] of this.rooms) if (r.emptySince && now - r.emptySince > PARTY.emptyRoomMs) { clearTimeout(r.timer); this.rooms.delete(code); this.listeners.delete(code); } }
}
