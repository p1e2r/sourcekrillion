// SourceKrillion Docker entry point: Node HTTP server + SQLite + WebSocket party rooms.
import http from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { join, normalize as normalizePath, extname, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import app, { playerFrom } from './app.mjs';
import { openDatabase } from './db.mjs';
import { dailyJob } from './generate.mjs';
import { PartyHall } from './party.mjs';
import { unlimitedPool } from './puzzles.mjs';
import { PACKS } from './content/packs.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', 'public');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.png': 'image/png', '.webp': 'image/webp', '.woff2': 'font/woff2', '.json': 'application/json', '.txt': 'text/plain; charset=utf-8', '.ico': 'image/x-icon' };
const ASSETS = {
  async fetch(request) {
    const path = decodeURIComponent(new URL(request.url).pathname);
    const file = normalizePath(join(ROOT, path));
    if (!file.startsWith(ROOT)) return new Response('Not found', { status: 404 });
    try {
      if (!(await stat(file)).isFile()) throw new Error('not a file');
      return new Response(request.method === 'HEAD' ? null : await readFile(file), { headers: { 'Content-Type': TYPES[extname(file)] || 'application/octet-stream' } });
    } catch { return new Response('Not found', { status: 404 }); }
  },
};

const env = { ...process.env, DB: openDatabase(process.env.DATABASE_PATH || '/data/sourcekrillion.db'), ASSETS };
env.PARTY = new PartyHall({ prompts: [...await unlimitedPool(env), ...PACKS.flatMap(p => p.chapters.flatMap(c => c.prompts))] });

const server = http.createServer(async (req, res) => {
  try {
    const origin = `${req.headers['x-forwarded-proto'] || 'http'}://${req.headers['x-forwarded-host'] || req.headers.host || 'localhost'}`;
    const chunks = []; for await (const c of req) chunks.push(c);
    const body = ['GET', 'HEAD'].includes(req.method) ? undefined : Buffer.concat(chunks);
    const request = new Request(origin + req.url, { method: req.method, headers: Object.entries(req.headers).flatMap(([k, v]) => Array.isArray(v) ? v.map(x => [k, x]) : [[k, v]]), body });
    const response = await app.fetch(request, env, {});
    const headers = {}; response.headers.forEach((v, k) => { headers[k] = v; });
    const cookies = response.headers.getSetCookie?.(); if (cookies?.length) headers['set-cookie'] = cookies;
    res.writeHead(response.status, headers);
    res.end(response.body ? Buffer.from(await response.arrayBuffer()) : undefined);
  } catch (e) { console.error(e); if (!res.headersSent) res.writeHead(500); res.end(); }
});

// Party rooms: ws://host/api/party/ws?room=CODE&name=NAME
const wss = new WebSocketServer({ noServer: true, maxPayload: 4096 });
server.on('upgrade', (req, socket, head) => {
  const url = new URL(req.url, 'http://x');
  if (url.pathname !== '/api/party/ws') return socket.destroy();
  const player = playerFrom({ headers: new Headers({ cookie: req.headers.cookie || '' }) }) || url.searchParams.get('guest');
  const room = env.PARTY.get(url.searchParams.get('room'));
  if (!player || !/^[a-f0-9-]{36}$/.test(player) || !room) { socket.write('HTTP/1.1 404 Not Found\r\n\r\n'); return socket.destroy(); }
  wss.handleUpgrade(req, socket, head, ws => {
    const send = msg => { if (ws.readyState === 1) ws.send(JSON.stringify(msg)); };
    try { room.join(player, url.searchParams.get('name')); } catch (e) { send({ t: 'error', message: e.message }); return ws.close(); }
    env.PARTY.subscribe(room.code, player, send);
    send({ t: 'state', room: room.view(player) });
    let last = 0;
    ws.on('message', raw => {
      if (Date.now() - last < 150) return; last = Date.now();
      let msg; try { msg = JSON.parse(raw); } catch { return; }
      try {
        if (msg.t === 'start') room.start(player);
        else if (msg.t === 'reset') room.reset(player);
        else if (msg.t === 'answer') { const r = room.answer(player, msg.text); send({ t: 'answer', ...r }); }
        else if (msg.t === 'ping') send({ t: 'pong', serverNow: Date.now() });
      } catch (e) { send({ t: 'error', message: e.message }); }
    });
    ws.on('close', () => { env.PARTY.unsubscribe(room.code, player); room.leave(player); });
  });
});

// Daily generation: check every 10 minutes; generation runs ahead for tomorrow.
let jobRunning = false;
const job = () => { if (jobRunning) return; jobRunning = true; dailyJob(env).then(r => r.length && console.log('Daily job:', JSON.stringify(r))).catch(e => console.error('Daily job failed:', e.message)).finally(() => { jobRunning = false; }); };
setInterval(job, 10 * 60 * 1000).unref(); setTimeout(job, 2000).unref();
setInterval(() => env.PARTY.sweep(), 30000).unref();

const port = Number(process.env.PORT || 3051);
server.listen(port, () => console.log(`SourceKrillion listening on :${port} (Groq ${env.GROQ_API_KEY ? 'on' : 'off'}, deep word ${env.DEEP_WORD ? 'set' : 'unset'})`));
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => { server.close(); env.DB.close(); process.exit(0); });
