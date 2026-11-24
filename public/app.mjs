import { TIERS, BANDS, ROUND_MS, bandFor, shareText, etDate } from './rules.mjs';
import { Ocean } from './ocean.mjs';
import { Sound } from './sound.mjs';
import { DROP_MS, easeInOut, easeOut, iconFor, tierImage, diveChart, distributionChart } from './presentation.mjs';

const $ = id => document.getElementById(id);
const escape = value => String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const testMode = ['localhost', '127.0.0.1'].includes(location.hostname) && new URLSearchParams(location.search).has('test');
const read = (key, fallback) => { try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; } };
const write = (key, value) => { try { localStorage.setItem(key, JSON.stringify(value)); } catch { /* private browsing */ } };
const settings = { lowPower: false, colorblind: false, corrections: true, clearRejected: false, shareLink: true, buddy: 'krill', ...read('sk:settings', {}) };
const scene = new Ocean($('ocean'), $('mascot')), sound = new Sound();
let today = null, game = null, phase = 'intro', busy = false, clockOffset = 0, autoSent = false, correction = null, lastTick = -1, review = null, currentScore = 0, paused = false, dialogPausePending = null, clockAtServer = 0, visualScore = 0, scoreAnimation = null, promptExitTimer = null, logbookCache = null;
const now = () => Date.now() + clockOffset + clockAtServer;
const wait = ms => new Promise(resolve => setTimeout(resolve, testMode ? Math.min(ms, 40) : ms));
async function api(path, data) {
  const response = await fetch('/api/' + path, { credentials: 'same-origin', cache: 'no-store', ...(data === undefined ? {} : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) }), signal: AbortSignal.timeout(15000) });
  const body = await response.json(); if (!response.ok) throw new Error(body.error || 'The signal is lost. Please retry.'); return body;
}
function setPhase(value) { const card = $('prompt-card'), exiting = value === 'plunge' && !card.hidden; clearTimeout(promptExitTimer); card.classList.toggle('leaving', exiting); if (exiting) promptExitTimer = setTimeout(() => { if (phase === 'plunge') card.hidden = true; }, testMode ? 40 : 650); phase = value; if (value !== 'answering') document.body.classList.remove('timer-hot'); document.body.dataset.phase = value; scene.setPhase(value); $('hero').hidden = false; $('intro').hidden = value !== 'intro'; card.hidden = !exiting && !['preview', 'answering'].includes(value); $('answer-dock').hidden = value !== 'answering'; $('preview-label').hidden = value !== 'preview'; $('landing').hidden = value !== 'landed'; $('next-wrap').hidden = value !== 'landed'; $('results').hidden = value !== 'done'; $('falling-answer').hidden = value !== 'plunge'; $('tier-ladder').hidden = value !== 'plunge'; }
const buddyOptions = [{ id: 'krill', name: 'The Krill', streak: 0, src: '/assets/krill.svg' }, { id: 'schooler', name: 'Schooler', streak: 5, src: '/assets/schooler.svg' }, { id: 'rare', name: 'Rare Squid', streak: 14, src: '/assets/rare.svg' }, { id: 'deepcut', name: 'Deep Cut Angler', streak: 30, src: '/assets/deepcut.svg' }, { id: 'evil', name: 'Evil Krill', streak: 45, src: '/assets/evil.svg' }];
function applySettings() { scene.lowPower = settings.lowPower; document.body.classList.toggle('low-power', settings.lowPower); document.body.classList.toggle('colorblind', settings.colorblind); document.querySelectorAll('[data-setting]').forEach(input => input.checked = !!settings[input.dataset.setting]); sound.mute(!!settings.muted); $('mute-btn').setAttribute('aria-pressed', String(!!settings.muted)); $('mute-btn').setAttribute('aria-label', settings.muted ? 'Turn sound on' : 'Turn sound off'); $('mute-btn').innerHTML = `<svg viewBox="0 0 20 16" fill="currentColor" shape-rendering="crispEdges" aria-hidden="true"><path d="M2 6h3l4-4v12l-4-4H2z"/>${settings.muted ? '<path d="m12 5 5 6m0-6-5 6" stroke="currentColor" stroke-width="2"/>' : '<path d="M11 6h2v4h-2zm4-3h2v10h-2z"/>'}</svg>`; $('mascot').dataset.buddy = settings.buddy; const buddy = buddyOptions.find(b => b.id === settings.buddy) || buddyOptions[0]; $('mascot').querySelector('img').src = buddy.src; $('mascot').querySelector('img').alt = buddy.name; syncBuddyGold(); }
applySettings();
const totalRounds = () => game?.prompts?.length || 7;
function updateHud() { const results = game?.results || []; currentScore = results.reduce((s, r) => s + r.score, 0); $('score').textContent = Math.round(visualScore); $('depth').textContent = Math.round(scene.depth).toLocaleString('en-US') + 'm'; $('round-label').textContent = game ? (game.completed ? 'DIVE COMPLETE' : `PROMPT ${Math.min(7, game.round + 1)} OF 7`) : 'SURFACE'; document.querySelectorAll('#round-dots i').forEach((dot, i) => { const r = results.find(r => r.round === i); dot.className = r ? 'done' : i === (game?.round ?? -1) ? 'active' : ''; dot.style.setProperty('--dot-color', r ? '#ffd166' : '#26364b'); }); }
function saveRun() { if (!game) return; write('sk:run', { runId: game.runId, date: game.date, mode: game.mode }); }
function setGame(value) { game = value; clockOffset = 0; clockAtServer = typeof game.serverNow === 'number' ? game.serverNow - Date.now() : 0; visualScore = game.results.reduce((sum, r) => sum + r.score, 0); saveRun(); updateHud(); }
function setHero(mode) {
  const copy = { daily: ['THE DAILY DIVE', '7 prompts · 25 seconds each · rarer answers sink deeper'], unlimited: ['THE ENDLESS DIVE', 'dive after dive · rarer answers sink deeper'], archive: ['THE ARCHIVE', 'every past dive · replays don’t count toward streaks'], pack: ['THEMED PACKS', 'beyond the daily dive · chapter by chapter'] }[mode] || ['THE DAILY DIVE', '7 prompts · 25 seconds each · rarer answers sink deeper'];
  $('tagline').textContent = copy[0]; $('subline').textContent = copy[1];
}
async function begin(options = {}) {
  if (busy) return; busy = true; $('start-btn').disabled = true; $('boot-error').hidden = true; closeParty(); await sound.init();
  try { const response = await api('start', options); setGame(response); setHero(game.mode); sound.event('begin'); scene.targetActive = 1; review = null; if (game.completed) { await showResults(); return; } if (game.roundStart === null && game.results.length) { const last = game.results.at(-1); showLanding({ ...last, promptText: game.prompts[last.round]?.text }); } else showRound(); }
  catch (e) { setPhase('intro'); $('boot-error').textContent = e.message; $('boot-error').hidden = false; }
  finally { busy = false; $('start-btn').disabled = false; }
}
function showRound() { setPhase('preview'); syncBuddyGold(); correction = null; autoSent = false; lastTick = -1; $('answer-status').textContent = ''; $('answer-status').className = 'answer-status'; const p = game.prompts[game.round]; $('prompt-number').textContent = `PROMPT ${game.round + 1} OF 7`; $('prompt-text').textContent = p.text; $('answer').value = read('sk:draft:' + game.runId + ':' + game.round, ''); $('answer').disabled = true; $('submit-btn').disabled = true; scene.setDepth(currentScore * 10); scene.roundBase = currentScore * 10; scene.roundIndex = game.round; scene.round(currentScore * 10); tick(); }
async function submit(timeout = false) {
  if (!game || busy || !['preview', 'answering'].includes(phase) || paused) return; const raw = $('answer').value.trim(); if (!raw && !timeout) return; busy = true; $('submit-btn').disabled = true;
  try {
    const response = await api('answer', { runId: game.runId, round: game.round, answer: raw, timeout, confirm: correction === raw, previewCorrection: settings.corrections });
    if (response.rejected) { $('answer-status').className = 'answer-status'; $('answer-status').textContent = response.message; sound.event('reject'); $('answer').classList.remove('shake'); void $('answer').offsetWidth; $('answer').classList.add('shake'); if (settings.clearRejected) $('answer').value = ''; correction = null; }
    else if (response.correction) { correction = response.correction; $('answer-status').className = 'answer-status correction'; $('answer-status').innerHTML = `“${escape(raw)}” → corrects to “${escape(correction)}”<small>submit again to confirm · or edit</small>`; $('answer').value = correction; write('sk:draft:' + game.runId + ':' + game.round, correction); }
    else if (response.alreadyScored) { setGame(response.state); if (game.completed) await showResults(); else if (game.roundStart === null) showLanding(game.results.at(-1)); else showRound(); }
    else { const oldScore = currentScore; setGame(response.state); sound.event('submit'); { const rect = $('answer').getBoundingClientRect(); for (let i = 0; i < 12; i++) scene.typeAt(rect.left + Math.random() * rect.width, rect.top + Math.random() * rect.height, 1); } await plunge(response.result, oldScore); }
  } catch (e) { $('answer-status').textContent = e.message; $('answer-status').className = 'answer-status'; }
  finally { busy = false; if (phase === 'answering') { $('submit-btn').disabled = !$('answer').value.trim(); $('answer').focus({ preventScroll: true }); } }
}
function animateScore(from, to, gold = false) {
  if (scoreAnimation) cancelAnimationFrame(scoreAnimation);
  const started = performance.now(), duration = testMode ? 60 : gold ? 1100 : 700;
  const frame = t => { const p = Math.min(1, (t - started) / duration), e = 1 - (1 - p) ** 4; visualScore = from + (to - from) * e; scene.depth = visualScore * 10; $('score').textContent = Math.round(visualScore); if (p < 1) scoreAnimation = requestAnimationFrame(frame); else { visualScore = to; if (gold) { $('score').classList.add('slam'); setTimeout(() => $('score').classList.remove('slam'), 520); } } };
  scoreAnimation = requestAnimationFrame(frame);
}
async function plunge(result, oldScore) {
  setPhase('plunge'); document.body.dataset.tier = result.tier || 'miss'; visualScore = oldScore; updateHud(); scene.roundIndex = game.round;
  const fall = $('falling-answer'), ladder = $('tier-ladder'); fall.textContent = result.answer ? `“${result.answer.toUpperCase()}”` : ''; fall.style.color = TIERS[result.tier]?.color || '#7d93b8'; fall.style.setProperty('--chip-color', TIERS[result.tier]?.color || '#7d93b8');
  ladder.innerHTML = result.tier ? Object.entries(TIERS).map(([id, t]) => `<div class="tier-line" data-tier="${id}" data-score="${t.score}" style="--line-color:${t.color}"><span>${tierImage(id)}${t.name.toUpperCase()} · ${t.score}</span></div>`).join('') : '';
  const duration = testMode ? 80 : DROP_MS[result.tier || 'miss'], began = performance.now(); let lastPassed = -1;
  if (result.tier) sound.event('sink'); else { sound.event('miss'); $('game').classList.add('shake'); setTimeout(() => $('game').classList.remove('shake'), 500); }
  await new Promise(resolve => { const frame = t => { const p = Math.min(1, (t - began) / duration), e = easeInOut(p); scene.drop(oldScore * 10, result, p); const sway = result.tier === 'plankton' ? 36 * Math.sin(p * 7) * (1 - .25 * p) : result.tier === 'tooclever' ? 7 * Math.sin(p * 42) : Math.sin(p * (result.tier === 'krillion' ? 24 : 16)) * (1 - p) * (result.tier === 'krillion' ? 26 : 14); fall.style.left = `calc(50% - 24px + ${sway}px)`; fall.style.rotate = (sway * (result.tier === 'plankton' ? .55 : .4)) + 'deg'; fall.style.top = (scene.layout().height / 2 - 60) + 'px';
    const travelled = 90 + (result.score * 10 - 90) * e; Array.from(ladder.children).forEach((line, i) => { if (travelled >= Number(line.dataset.score) * 10 - 4 && !line.classList.contains('active')) { line.classList.add('active'); if (i > lastPassed) { sound.event('tier'); lastPassed = i; } } });
    if (p < 1) requestAnimationFrame(frame); else { scene.cameraDepth = scene.cameraTarget; resolve(); } }; requestAnimationFrame(frame); });
  showLanding(result, oldScore);
}
function showLanding(result, oldScore = currentScore - result.score) {
  setPhase('landed'); const tier = TIERS[result.tier]; document.body.dataset.tier = result.tier || 'miss'; document.documentElement.style.setProperty('--tier', tier?.color || '#7d93b8');
  const icon = $('land-icon'); icon.hidden = !tier; icon.src = '/assets/' + (iconFor(result.tier) || 'plankton') + '.svg'; icon.style.visibility = result.tier === 'krillion' ? 'hidden' : 'visible';
  $('land-tier').textContent = tier?.name.toUpperCase() || 'NOTHING LANDED'; $('land-answer').textContent = result.answer ? `“${result.answer}”` : '—'; $('land-points').innerHTML = `<b>+${result.score} PTS</b>${result.score ? ' · sink ' + (result.score * 10).toLocaleString('en-US') + 'm' : ''}`;
  $('land-quip').textContent = result.quip || tier?.blurb || (result.rawAnswer ? 'never made it onto the reef.' : 'the clock beat you to it.'); $('next-btn').textContent = game.completed ? 'SURFACE ▲' : 'DESCEND ▼';
  if (tier) sound.event(result.tier); scene.target = currentScore * 10; scene.roundIndex = game.round;
  if (!scene.fx) scene.fx = { tier: result.tier, progress: 1 };
  scene.moveCamera(currentScore * 10 + 30, testMode ? 1 : 500);
  const gold = result.tier === 'krillion'; visualScore = oldScore;
  if (gold) { $('mascot').classList.remove('celebrate'); void $('mascot').offsetWidth; $('mascot').classList.add('celebrate'); setTimeout(() => $('mascot').classList.remove('celebrate'), testMode ? 30 : 1250); scene.goldLandAt = performance.now(); setTimeout(() => { if (phase !== 'landed' || game.results.at(-1)?.round !== result.round) return; syncBuddyGold(); scene.burst('#ffd166', 42); animateScore(oldScore, currentScore, true); }, testMode ? 20 : 1000); } else animateScore(oldScore, currentScore);
  if (['rare', 'tooclever'].includes(result.tier)) { $('game').classList.add('shake'); setTimeout(() => $('game').classList.remove('shake'), 500); }
  if (result.score >= 60) { scene.burst(tier.color, result.score === 100 ? 36 : 22); const f = document.querySelector('.flash'); f.style.background = tier.color; f.classList.remove('on'); void f.offsetWidth; f.classList.add('on'); }
  if (!gold) syncBuddyGold(); updateHud();
}
async function next() { if (busy) return; busy = true; try { if (game.completed) { sound.event('surface'); const duration = Math.min(9000, Math.max(2500, (currentScore * 10 + 30) * 3)); scene.moveCamera(-45, testMode ? 1 : duration, easeOut); await showResults(); return; } sound.event('begin'); const data = await api('next', { runId: game.runId }); setGame(data); showRound(); } catch (e) { $('land-quip').textContent = e.message; } finally { busy = false; } }
function syncBuddyGold() { const gold = game?.results.some(r => r.tier === 'krillion'); $('mascot').classList.toggle('gold', !!gold); if (settings.buddy === 'krill') $('mascot').querySelector('img').src = gold ? '/assets/krill-gold.svg' : '/assets/krill.svg'; }
async function loadLogbook() { try { logbookCache = await api('logbook'); } catch { logbookCache = logbookCache || { stats: { current: 0, bestStreak: 0, played: 0, best: 0, average: 0 }, runs: [] }; } return logbookCache; }
function storeHistory() { const history = read('sk:history', {}), key = game.mode === 'daily' ? game.date : game.runId; history[key] = { date: game.date, dayNumber: game.dayNumber, mode: game.mode, title: game.title, score: currentScore, results: game.results }; write('sk:history', history); }
function resultActions() {
  if (game.mode === 'unlimited') return `<button class="quiet" data-action="unlimited-next">NEXT DIVE ∞</button><button class="quiet" data-action="daily">DAILY DIVE ↓</button><button class="quiet" data-action="packs">PACKS ▤</button>`;
  if (game.mode === 'pack') { const more = game.pack.chapter + 1 < game.pack.chapters; return `${more ? `<button class="quiet" data-action="pack-next">NEXT ${escape(game.title.split('·')[1].trim().split(' ')[0].toUpperCase())} →</button>` : ''}<button class="quiet" data-action="packs">ALL PACKS ▤</button><button class="quiet" data-action="unlimited">UNLIMITED ∞</button>`; }
  if (game.mode === 'archive') return `<button class="quiet" data-action="archive">ARCHIVE ⟲</button><button class="quiet" data-action="unlimited">UNLIMITED ∞</button><button class="quiet" data-action="friends">FRIENDS ☻</button>`;
  return `<button class="quiet" data-action="unlimited">KEEP DIVING ∞</button><button class="quiet" data-action="friends">FRIENDS ☻</button><button class="quiet" data-action="archive">ARCHIVE ⟲</button>`;
}
async function showResults() {
  setPhase('done'); visualScore = currentScore; syncBuddyGold(); scene.setDepth(0); review = null; storeHistory(); const band = bandFor(currentScore), log = await loadLogbook(), stats = log.stats; document.documentElement.style.setProperty('--tier', TIERS[band.tier].color);
  const ranked = game.mode === 'daily' || game.mode === 'archive';
  $('results').innerHTML = `<header class="result-header"><span class="result-brand">SOURCEKRILLION</span><span>${escape(game.title)} complete</span></header><div class="result-score"><strong>${currentScore}</strong><span>-${(currentScore * 10).toLocaleString('en-US')}m</span></div><div class="distribution" id="distribution"><p>Sounding this ocean’s depths…</p></div><p class="rule">DIVE LOG · DEEPER = RARER</p>${diveChart(game.results, escape)}<p class="rule">THE BEARING</p><ul class="band-list">${BANDS.map(b => `<li class="${b === band ? 'current' : ''}">${tierImage(b.tier)}<span class="band-range" style="color:${TIERS[b.tier].color}">${b.min}${b.min === 450 ? '+' : '–' + b.max}</span><span>${b.name}. ${b.text}</span></li>`).join('')}</ul><div class="share-actions"><button class="quiet" id="copy-result">Copy result</button>${navigator.share ? '<button class="quiet" id="native-share">Share…</button>' : ''}</div><div class="actions">${resultActions()}</div><p class="rule">THE CATCH · TAP A ROUND FOR EVERY ANSWER</p><ul class="answer-review">${game.results.map((r, i) => `<li><button class="review-toggle" aria-expanded="false" data-review="${i}" style="--dot-color:${TIERS[r.tier]?.color || '#43556b'}"><span class="review-number">${i + 1}</span><i class="review-dot">${tierImage(r.tier)}</i><span class="review-question">${escape(game.prompts[i].text)}<small>${escape(r.answer || '—')}</small></span><span class="review-points">${r.score}</span><span class="review-sign">+</span></button><div class="review-expanded" hidden></div></li>`).join('')}</ul><p class="rule">LOGBOOK</p><p class="logbook-line">streak ${stats.current} · played ${stats.played} · avg ${stats.average} · best ${stats.best}</p><p class="reset-countdown" id="reset-countdown"></p><form class="suggestion" id="suggestion-form"><label for="suggestion">got an idea for a question? tell us what to ask</label><div><input id="suggestion" placeholder="name a type of cloud…" maxlength="1000"><button class="quiet" type="submit">submit</button></div><p class="muted" id="suggestion-status"></p></form><div><p class="muted">how was that dive? 1 = meh · 5 = great</p><div class="rating" role="group" aria-label="Rate your dive">${[1, 2, 3, 4, 5].map(n => `<button data-rating="${n}" aria-pressed="false" aria-label="${n} out of 5">${n}</button>`).join('')}</div><p class="muted" id="rating-status"></p></div><footer class="original-footer result-footer"><p>one in a krillion. Open source. Open ocean.</p><nav><button class="text-button" data-action="faq">FAQ</button><a href="/privacy">Privacy</a><a href="/about">About</a></nav><p class="independent">Independent Krillion-inspired game. Not affiliated with krillion.io.</p></footer>`;
  $('results').scrollTop = 0; updateHud();
  try { review = await api('review?run=' + encodeURIComponent(game.runId)); const d = $('distribution'); if (phase !== 'done') return; if (!ranked) d.innerHTML = distributionChart([], currentScore, 0) + `<p>${game.mode === 'pack' ? escape(game.pack.name) + ' · ' + escape(game.pack.chapterTitle) : 'Unlimited dive · your daily score stays unchanged.'}</p>`; else if (review.betterThan === null) d.innerHTML = distributionChart(review.dist, currentScore, review.population) + '<p>YOUR SCORE · MORE DIVERS NEEDED FOR COMPARISON</p>'; else d.innerHTML = distributionChart(review.dist, currentScore, review.population) + `<p>BETTER THAN ${review.betterThan}% OF ${review.population} OTHER DIVERS</p>`; } catch { $('distribution').innerHTML = '<p>Comparison temporarily unavailable. Your score is saved.</p>'; }
}
function resultText() { return shareText(game.shareLabel || '#' + game.dayNumber, game.results, settings.shareLink ? location.origin : ''); }
async function copyResult(button) { try { await navigator.clipboard.writeText(resultText()); button.textContent = 'Copied ✓'; setTimeout(() => button.textContent = 'Copy result', 2200); } catch { button.textContent = 'Select and copy below'; const box = document.createElement('textarea'); box.value = resultText(); box.readOnly = true; box.style.width = '100%'; button.parentElement.after(box); box.select(); } }
async function expandReview(button) { const i = Number(button.dataset.review), box = button.nextElementSibling, open = button.getAttribute('aria-expanded') === 'true'; button.setAttribute('aria-expanded', String(!open)); button.querySelector('.review-sign').textContent = open ? '+' : '−'; box.hidden = open; if (open) return; if (!review) { box.innerHTML = '<p class="muted">Loading accepted answers…</p>'; try { review = await api('review?run=' + encodeURIComponent(game.runId)); } catch { box.innerHTML = '<p class="muted">Could not load answers. Close and try again.</p>'; return; } } const answers = review.prompts[i].answers, own = game.results[i].answer; box.innerHTML = `${answers.length > 30 ? '<input class="search-answers" aria-label="Search accepted answers" placeholder="search ' + answers.length + ' answers…">' : ''}<ul class="review-list">${answers.map(a => `<li data-answer="${escape(a.answer.toLowerCase())}" style="color:${TIERS[a.tier].color}"><span>${escape(a.answer)}${a.answer === own ? '<b class="you">YOU</b>' : ''}${a.quip ? '<small>' + escape(a.quip) + '</small>' : ''}</span><span>+${a.score}</span></li>`).join('')}</ul>`; }

// ---------- panels ----------
const modeLabel = r => r.mode === 'daily' ? `Dive #${r.dayNumber}` : r.mode === 'archive' ? `Archive #${r.dayNumber}` : r.mode === 'unlimited' ? `∞ Dive #${r.number || ''}` : `Pack · ${escape(r.puzzle.split(':')[1])} ${Number(r.puzzle.split(':')[2]) + 1}`;
async function openLogbook() {
  $('logbook-content').innerHTML = '<p class="muted">Opening your logbook…</p>'; openDialog('logbook-dialog');
  const log = await loadLogbook(), s = log.stats;
  $('logbook-content').innerHTML = `<div class="stats-grid"><div><strong>${s.current}</strong><span>Current streak</span></div><div><strong>${s.bestStreak}</strong><span>Best streak</span></div><div><strong>${s.played}</strong><span>Daily dives</span></div><div><strong>${s.best}</strong><span>Best daily score</span></div></div><ul class="history-list">${log.runs.slice(0, 40).map(r => `<li><span>${modeLabel(r)} <small>${escape(r.date)}</small></span><span>${r.score} pts · ${(r.score * 10).toLocaleString('en-US')}m</span></li>`).join('') || '<li>Your next dive starts your logbook.</li>'}</ul>`;
  renderSync();
}
async function renderSync(code) {
  let me = { hasRestoreCode: false }; try { me = await api('me'); } catch { }
  $('sync-content').innerHTML = `${code ? `<p class="restore-code" aria-label="Your restore code">${escape(code)}</p><p class="muted">Write this down. Enter it on another device to open this logbook there. Making a new code replaces this one.</p>` : `<p class="muted">${me.hasRestoreCode ? 'You have a restore code. Make a new one if you lost it (the old one stops working).' : 'Make a restore code to open this logbook on another device.'}</p>`}<button class="quiet" id="make-restore">${me.hasRestoreCode || code ? 'New restore code' : 'Make a restore code'}</button><form class="inline-form" id="restore-form"><label class="muted" for="restore-input">have a code from another device?</label><div><input id="restore-input" placeholder="XXXX-XXXX-XXXX" maxlength="16" autocapitalize="characters" spellcheck="false"><button class="quiet" type="submit">restore</button></div><p class="muted" id="restore-status"></p></form>`;
}
async function openArchive() {
  openDialog('archive-dialog'); const list = $('archive-list');
  try { const { days } = await api('archive'); list.innerHTML = days.length ? days.map(d => `<li><button class="archive-day" data-archive="${d.date}"><span><strong>Dive #${d.dayNumber}</strong><small>${escape(new Date(d.date + 'T12:00:00Z').toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric', year: 'numeric' }))}</small></span><span class="${d.completed ? 'done' : ''}">${d.completed ? d.score + ' pts' : d.played ? 'resume →' : 'dive →'}</span></button></li>`).join('') : '<li class="muted">The archive opens tomorrow. Today’s dive is the first.</li>'; }
  catch (e) { list.innerHTML = `<li class="error">${escape(e.message)}</li>`; }
}
async function openPacks() {
  openDialog('packs-dialog'); const box = $('packs-list');
  try { const { packs } = await api('packs'); box.innerHTML = packs.map(p => `<section class="pack-card pixel-frame"><p class="eyebrow">${escape(p.name.toUpperCase())}</p><p class="pack-tag">${p.chapters.length} ${escape(p.unit)}s · ${escape(p.tagline)}</p><div class="pack-meter"><i style="width:${p.completed / p.chapters.length * 100}%"></i></div><ol class="chapter-list">${p.chapters.map(c => `<li><button data-pack="${p.id}" data-chapter="${c.index}" ${c.unlocked ? '' : 'disabled'}><span>${c.index + 1}. ${escape(c.title)}</span><span>${c.completed ? c.best + ' pts' : c.unlocked ? 'dive →' : 'locked'}</span></button></li>`).join('')}</ol></section>`).join(''); }
  catch (e) { box.innerHTML = `<p class="error">${escape(e.message)}</p>`; }
}
async function openFriends() {
  openDialog('friends-dialog'); const box = $('friends-content');
  try {
    let view = await api('friends');
    if (!view.me) { await api('me', { name: '' }); view = await api('friends'); }
    box.innerHTML = `<div class="friend-code"><span class="muted">your friend code</span><strong>${escape(view.me.friendCode)}</strong><button class="quiet" id="copy-code">copy</button></div><form class="inline-form" id="name-form"><label class="muted" for="name-input">your diver name</label><div><input id="name-input" value="${escape(view.me.name)}" placeholder="name yourself…" maxlength="18"><button class="quiet" type="submit">save</button></div></form><form class="inline-form" id="friend-form"><label class="muted" for="friend-input">add a friend by code</label><div><input id="friend-input" placeholder="ABC123" maxlength="8" autocapitalize="characters" spellcheck="false"><button class="quiet" type="submit">add</button></div><p class="muted" id="friend-status"></p></form><p class="rule">TODAY · DIVE #${view.dayNumber}</p>${view.revealed ? '' : '<p class="muted">Finish today’s dive to see your friends’ scores.</p>'}<ol class="leaderboard">${view.people.map((p, i) => `<li class="${p.you ? 'you' : ''}"><span class="rank">${p.score === null ? '·' : i + 1}</span><span class="who">${escape(p.name)}${p.you ? ' <b class="you">YOU</b>' : ''}<small>${p.streak ? p.streak + '-day streak' : 'no streak'} · ${escape(p.code)}</small></span><span class="tiers">${(p.tiers || []).map(t => TIERS[t] ? `<img src="/assets/${iconFor(t)}.svg" alt="" width="14" height="14">` : '<i class="miss-mini"></i>').join('')}</span><span class="pts">${p.score !== null ? p.score : p.played ? 'done' : '—'}</span>${p.you ? '' : `<button class="unfriend" data-unfriend="${escape(p.code)}" aria-label="Remove ${escape(p.name)}">×</button>`}</li>`).join('')}</ol>${view.people.length < 2 ? '<p class="muted">Share your code. When a friend adds you, you both appear here.</p>' : ''}`;
  } catch (e) { box.innerHTML = `<p class="error">${escape(e.message)}</p>`; }
}
function openFaq() { openDialog('faq-dialog'); }

async function pauseGame() { if (!game || !['preview', 'answering'].includes(phase) || paused) return; paused = true; try { dialogPausePending = api('pause', { runId: game.runId, paused: true }); setGame(await dialogPausePending); } catch { paused = false; } finally { dialogPausePending = null; } }
async function resumeGame() { if (document.querySelector('dialog[open]') || !paused) return; const rid = game.runId; try { if (dialogPausePending) await dialogPausePending; const resumed = await api('pause', { runId: rid, paused: false }); if (game.runId === rid) setGame(resumed); paused = false; tick(); } catch { $('answer-status').textContent = 'Could not resume. Reopen the menu and try again.'; } }
function openDialog(id) { document.querySelectorAll('dialog[open]').forEach(d => { if (d.id !== id) d.close(); }); if (!$(id).open) $(id).showModal(); void pauseGame(); }
function closeDialogs() { document.querySelectorAll('dialog[open]').forEach(d => d.close()); }
function resumeDaily() { if (game?.mode === 'daily') { if (game.completed) void showResults(); else if (game.roundStart === null) showLanding(game.results.at(-1)); else showRound(); } else void begin({ mode: 'daily' }); }
function doAction(action) {
  const keepOpen = ['logbook', 'archive', 'packs', 'friends', 'faq', 'settings'].includes(action);
  if (!keepOpen) closeDialogs();
  if (action === 'daily') { closeParty(); resumeDaily(); }
  if (action === 'unlimited' || action === 'practice') void begin({ mode: 'unlimited' });
  if (action === 'unlimited-next') void begin({ mode: 'unlimited', fresh: true });
  if (action === 'pack-next') void begin({ mode: 'pack', pack: game.pack.id, chapter: game.pack.chapter + 1 });
  if (action === 'party') openParty();
  if (action === 'archive') openArchive();
  if (action === 'packs') openPacks();
  if (action === 'friends') openFriends();
  if (action === 'faq') openFaq();
  if (action === 'settings') openDialog('settings-dialog');
  if (action === 'logbook') openLogbook();
}
function openWardrobe() { loadLogbook().then(log => { const best = log.stats.bestStreak; $('buddy-list').innerHTML = buddyOptions.map(b => `<button data-buddy="${b.id}" aria-pressed="${settings.buddy === b.id}" ${best < b.streak ? 'disabled' : ''}><img src="${b.src}" alt=""><span>${b.name}</span><small>${best >= b.streak ? 'Earned' : b.streak + '-day streak'}</small></button>`).join(''); openDialog('wardrobe-dialog'); }); }

// ---------- puffer party ----------
const party = { socket: null, room: null, me: null, lobbyTimer: null, lastPop: null, rejectedAt: 0, flashUntil: 0 };
function partyName() { return read('sk:party-name', '') || logbookCache?.me?.name || ''; }
async function openParty(code = null) {
  closeDialogs(); if (game && ['preview', 'answering'].includes(phase)) await pauseGame();
  document.body.dataset.party = 'on'; $('party').hidden = false; if (phase === 'done') setPhase('intro'); $('intro').hidden = true; $('hero').hidden = true;
  try { const me = await api('me'); if (!read('sk:party-name', '') && me.name) write('sk:party-name', me.name); } catch { }
  if (code) return joinRoom(code);
  renderLobby(); refreshLobby(); clearInterval(party.lobbyTimer); party.lobbyTimer = setInterval(refreshLobby, 5000);
}
function closeParty() {
  if ($('party').hidden) return;
  clearInterval(party.lobbyTimer); party.socket?.close(); party.socket = null; party.room = null; $('party').hidden = true; delete document.body.dataset.party;
  if (history.state?.party || location.pathname === '/party') history.replaceState({}, '', '/');
  if (phase === 'intro') { $('intro').hidden = false; $('hero').hidden = false; }
}
function renderLobby(rooms = null) {
  $('party').innerHTML = `<div class="party-sheet"><button class="close party-close" data-party-close aria-label="Leave party">×</button><header class="party-head"><span class="open-label">SOURCEKRILLION</span><h2 class="party-title">PUFFER PARTY</h2><p class="tagline">pass the puffer · don’t let it pop on you</p></header><img class="party-puffer idle" src="/assets/puffer.svg" alt="" width="120" height="120"><p class="muted party-blurb">Name an answer and pass the puffer before it pops. Play with friends or join a public room. Last one floating wins.</p><form class="inline-form" id="party-name-form"><label class="muted" for="party-name">your diver name</label><div><input id="party-name" value="${escape(partyName())}" placeholder="name yourself…" maxlength="18"></div></form><div class="party-actions"><button class="begin-btn small" data-party-create="public">OPEN A PUBLIC ROOM</button><button class="quiet" data-party-create="private">PRIVATE ROOM</button></div><form class="inline-form" id="party-join-form"><label class="muted" for="party-code">join with a room code</label><div><input id="party-code" placeholder="ABCD" maxlength="4" autocapitalize="characters" spellcheck="false"><button class="quiet" type="submit">join</button></div><p class="error" id="party-error"></p></form><p class="rule">PUBLIC ROOMS</p><ul class="room-list" id="room-list">${rooms === null ? '<li class="muted">Loading rooms…</li>' : ''}</ul></div>`;
}
async function refreshLobby() {
  if (party.room || $('party').hidden) return;
  try { const { rooms, enabled } = await api('party/rooms'); const list = $('room-list'); if (!list) return; list.innerHTML = !enabled ? '<li class="muted">Party rooms are off on this server.</li>' : rooms.length ? rooms.map(r => `<li><button data-party-join="${r.code}"><span><strong>${r.code}</strong><small>${r.status === 'playing' ? escape(r.prompt || 'in play') : 'waiting in the lobby'}</small></span><span>${r.players}/${r.max} ${r.status === 'playing' ? '· watching' : '· join →'}</span></button></li>`).join('') : '<li class="muted">No public rooms right now. Open one!</li>'; } catch { }
}
function savePartyName() { const v = $('party-name')?.value.trim(); if (v !== undefined) { write('sk:party-name', v); api('me', { name: v }).catch(() => { }); } return v; }
async function createRoom(isPublic) { try { const { code } = await api('party/create', { public: isPublic }); joinRoom(code); } catch (e) { const el = $('party-error'); if (el) el.textContent = e.message; } }
async function joinRoom(code) {
  code = String(code).toUpperCase().replace(/[^A-Z]/g, '').slice(0, 4); if (code.length !== 4) { const el = $('party-error'); if (el) el.textContent = 'Room codes have four letters.'; return; }
  const name = $('party-name') ? $('party-name').value.trim() : partyName(); write('sk:party-name', name);
  try { await api('me', { name }); } catch (e) { const el = $('party-error'); if (el) el.textContent = e.message; return; }
  sound.init(); clearInterval(party.lobbyTimer); party.socket?.close();
  const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/party/ws?room=${code}&name=${encodeURIComponent(partyName())}`);
  party.socket = ws; party.lastPop = null;
  history.replaceState({ party: true }, '', `/party?room=${code}`);
  ws.onmessage = e => { const msg = JSON.parse(e.data); if (msg.t === 'state') { const prev = party.room; party.room = msg.room; party.clockAt = msg.room.serverNow - Date.now(); onRoomChange(prev, msg.room); renderRoom(); } else if (msg.t === 'answer') { if (!msg.ok) { party.rejectedAt = Date.now(); sound.event('reject'); const s = $('party-status'); if (s) s.textContent = msg.message; } else sound.event('submit'); } else if (msg.t === 'error') { const s = $('party-status') || $('party-error'); if (s) s.textContent = msg.message; } };
  ws.onclose = () => { if (party.socket !== ws) return; party.socket = null; if (party.room) { party.room = null; renderLobby(); refreshLobby(); party.lobbyTimer = setInterval(refreshLobby, 5000); const el = $('party-error'); if (el) el.textContent = 'Disconnected from the room.'; } else { const el = $('party-error'); if (el) el.textContent = 'That room does not exist (or it is full).'; } };
}
function onRoomChange(prev, room) {
  if (room.round?.popped && room.round.popped !== party.lastPop) { party.lastPop = room.round.popped; party.flashUntil = Date.now() + 900; sound.event('miss'); $('game').classList.add('shake'); setTimeout(() => $('game').classList.remove('shake'), 500); }
  if (prev?.round && room.round && prev.round.holder !== room.round.holder && !room.round.popped) sound.event('tier');
  if (room.status === 'over' && prev?.status !== 'over') sound.event(room.winner === room.you ? 'krillion' : 'surface');
  if (room.status === 'playing' && prev?.status !== 'playing') sound.event('begin');
}
const nameOf = (room, id) => room.players.find(p => p.id === id)?.name || 'someone';
function renderRoom() {
  const room = party.room, me = room.you, host = room.host === me, r = room.round, holding = r && !r.popped && r.holder === me && room.status === 'playing';
  const draft = $('party-answer')?.value || '';
  const focused = document.activeElement?.id === 'party-answer';
  $('party').innerHTML = `<div class="party-sheet room"><button class="close party-close" data-party-close aria-label="Leave party">×</button><header class="party-room-head"><span class="eyebrow">ROOM ${room.code}${room.public ? ' · PUBLIC' : ' · PRIVATE'}</span><button class="text-button" id="copy-room">copy invite link</button></header>
  <ul class="party-players">${room.players.map(p => `<li class="${p.alive ? '' : 'out'} ${r && !r.popped && r.holder === p.id && room.status === 'playing' ? 'holding' : ''} ${p.id === me ? 'me' : ''}"><img src="/assets/${p.alive ? 'puffer' : 'puffer-angry'}.svg" alt="" width="22" height="22"><span>${escape(p.name)}${p.id === room.host ? ' ★' : ''}${p.connected ? '' : ' (away)'}</span><small>${p.alive ? p.answers + ' passed' : 'popped'}</small></li>`).join('')}</ul>
  ${room.status === 'lobby' ? `<div class="party-wait"><img class="party-puffer idle" src="/assets/puffer.svg" alt="" width="120" height="120"><p class="tagline">${room.players.length < 2 ? 'waiting for divers…' : host ? 'everyone in? start the round' : 'waiting for the host…'}</p>${host ? `<button class="begin-btn small" id="party-start" ${room.players.filter(p => p.connected).length < 2 ? 'disabled' : ''}>▼ START ▼</button>` : ''}<p class="muted">Share the code <b class="code">${room.code}</b> · up to 8 divers</p></div>` : ''}
  ${r && room.status !== 'lobby' ? `<section class="prompt-card party-prompt pixel-frame"><p class="eyebrow">ROUND ${room.roundNumber}</p><h2>${escape(r.prompt)}</h2><p class="hint">${r.popped ? `POP! ${escape(nameOf(room, r.popped))} is out` : holding ? 'you have the puffer · name one, quick' : `${escape(nameOf(room, r.holder))} has the puffer`}</p></section><div class="party-stage"><img id="puffer-img" class="party-puffer live ${r.popped ? 'popped' : ''}" src="/assets/${r.popped ? 'puffer-angry' : 'puffer'}.svg" alt="pufferfish" width="140" height="140"><p class="holder-name" id="holder-name">${r.popped ? '💥' : escape(nameOf(room, r.holder))}</p></div><ul class="used-answers">${r.used.slice(-14).map(u => `<li style="--chip:${TIERS[u.tier].color}">${escape(u.answer)}<small>${escape(nameOf(room, u.by))}</small></li>`).join('')}</ul>` : ''}
  ${room.status === 'over' ? `<div class="party-over"><p class="eyebrow">LAST ONE FLOATING</p><h2>${room.winner ? escape(nameOf(room, room.winner)) + (room.winner === me ? ' (you!)' : '') : 'nobody'}</h2>${host ? '<button class="begin-btn small" id="party-reset">PLAY AGAIN</button>' : '<p class="muted">waiting for the host to play again…</p>'}</div>` : ''}
  ${room.status === 'playing' ? `<div class="answer-dock party-dock" ${holding ? '' : 'data-waiting'}><div class="answer-status" id="party-status"></div><form id="party-form" autocomplete="off"><div class="input-wrap"><input id="party-answer" placeholder="${holding ? 'type one answer…' : 'wait for the puffer…'}" maxlength="120" ${holding ? '' : 'disabled'} autocomplete="off" autocapitalize="none" spellcheck="false" enterkeyhint="send"><div class="time-bar"></div></div><button id="party-pass" type="submit" ${holding ? '' : 'disabled'}>PASS</button></form></div>` : ''}</div>`;
  if (holding) { const input = $('party-answer'); input.value = draft; if (focused || true) input.focus({ preventScroll: true }); }
}
function partyFrame() {
  const room = party.room, img = $('puffer-img');
  if (room?.round && img && room.status === 'playing' && !room.round.popped) {
    const elapsed = Date.now() + (party.clockAt || 0) - room.round.startedAt, f = Math.min(1, elapsed / room.round.maxFuseMs), danger = elapsed > room.round.minFuseMs * .75;
    const wobble = danger && !scene.reduced ? Math.sin(Date.now() * .05) * (2 + f * 6) : 0;
    img.style.transform = `scale(${(0.8 + f * .9).toFixed(3)}) rotate(${wobble.toFixed(1)}deg)`; img.classList.toggle('danger', danger);
    document.documentElement.style.setProperty('--urgency', danger ? (f * .6).toFixed(2) : 0);
  }
  requestAnimationFrame(partyFrame);
}
requestAnimationFrame(partyFrame);

function tick() { if (game && ['preview', 'answering'].includes(phase) && !paused) { const remaining = game.roundStart + ROUND_MS - now(); if (now() < game.roundStart) { const count = Math.max(1, Math.ceil((game.roundStart - now()) / 1000)); $('preview-label').textContent = `descending · the clock starts in ${count}`; $('timer-number').textContent = count; document.documentElement.style.setProperty('--time', 1); } else { if (phase === 'preview') { setPhase('answering'); $('answer').disabled = false; $('preview-label').textContent = ''; $('answer').focus({ preventScroll: true }); } $('submit-btn').disabled = busy || !$('answer').value.trim(); const seconds = Math.max(0, Math.ceil(remaining / 1000)); document.body.classList.toggle('timer-hot', remaining <= 3050); $('timer-number').textContent = seconds; document.documentElement.style.setProperty('--time', Math.max(0, remaining / ROUND_MS)); document.documentElement.style.setProperty('--urgency', seconds <= 3 && !scene.reduced ? (1 - seconds / 4) * (.35 + Math.sin(Date.now() * .012) * .2) : 0); if (seconds > 0 && seconds <= 3 && seconds !== lastTick) { sound.event('tick'); lastTick = seconds; } if (remaining <= 0 && !autoSent && !busy) { autoSent = true; void submit(true); } } } else if (!party.room) document.documentElement.style.setProperty('--urgency', 0); if ($('reset-countdown') && game) { const d = Math.max(0, (game.resetsAt - Date.now()) / 1000), h = Math.floor(d / 3600), m = Math.floor(d % 3600 / 60), s = Math.floor(d % 60); $('reset-countdown').textContent = `next dive in ${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`; } if (game) $('depth').textContent = Math.round(scene.depth).toLocaleString('en-US') + 'm'; }

$('start-btn').addEventListener('click', () => resumeDaily());
$('menu-btn').addEventListener('click', () => openDialog('menu-dialog')); $('settings-btn').addEventListener('click', () => openDialog('settings-dialog')); $('wardrobe-btn').addEventListener('click', openWardrobe); $('next-btn').addEventListener('click', next);
$('mute-btn').addEventListener('click', async () => { await sound.init(); settings.muted = !settings.muted; write('sk:settings', settings); applySettings(); });
$('mascot').addEventListener('click', async () => { await sound.init(); sound.event('blub'); $('mascot').classList.remove('poked'); void $('mascot').offsetWidth; $('mascot').classList.add('poked'); setTimeout(() => $('mascot').classList.remove('poked'), 1000); });
$('answer-form').addEventListener('submit', e => { e.preventDefault(); void submit(false); });
$('answer').addEventListener('input', () => { const r = $('answer').getBoundingClientRect(); scene.typeAt(r.left + 14 + Math.min(11 * $('answer').value.length, r.width - 28), r.top + 5); write('sk:draft:' + game.runId + ':' + game.round, $('answer').value); if (correction !== $('answer').value) { correction = null; $('answer-status').textContent = ''; } tick(); });
document.addEventListener('click', e => {
  const t = e.target;
  const close = t.closest('[data-close]'); if (close) close.closest('dialog').close();
  const action = t.closest('[data-action]'); if (action) doAction(action.dataset.action);
  const r = t.closest('[data-review]'); if (r) void expandReview(r);
  const copy = t.closest('#copy-result'); if (copy) void copyResult(copy);
  if (t.closest('#native-share')) navigator.share?.({ text: resultText() }).catch(() => { });
  const buddy = t.closest('[data-buddy]'); if (buddy) { settings.buddy = buddy.dataset.buddy; write('sk:settings', settings); applySettings(); openWardrobe(); }
  const rating = t.closest('[data-rating]'); if (rating) api('feedback', { kind: 'rating', text: rating.dataset.rating }).then(() => { document.querySelectorAll('[data-rating]').forEach(b => b.setAttribute('aria-pressed', String(b === rating))); $('rating-status').textContent = 'Thanks for the feedback.'; }).catch(err => $('rating-status').textContent = err.message);
  const day = t.closest('[data-archive]'); if (day) { closeDialogs(); void begin({ mode: 'archive', date: day.dataset.archive }); }
  const chapter = t.closest('[data-chapter]'); if (chapter && !chapter.disabled) { closeDialogs(); void begin({ mode: 'pack', pack: chapter.dataset.pack, chapter: Number(chapter.dataset.chapter) }); }
  if (t.closest('#make-restore')) api('restore-code', {}).then(r => renderSync(r.code)).catch(err => $('restore-status').textContent = err.message);
  if (t.closest('#copy-code')) navigator.clipboard?.writeText(document.querySelector('.friend-code strong').textContent).then(() => { t.textContent = 'copied ✓'; });
  const unfriend = t.closest('[data-unfriend]'); if (unfriend) api('friends/remove', { code: unfriend.dataset.unfriend }).then(openFriends).catch(() => { });
  if (t.closest('[data-party-close]')) closeParty();
  const create = t.closest('[data-party-create]'); if (create) void createRoom(create.dataset.partyCreate === 'public');
  const join = t.closest('[data-party-join]'); if (join) joinRoom(join.dataset.partyJoin);
  if (t.closest('#party-start')) party.socket?.send(JSON.stringify({ t: 'start' }));
  if (t.closest('#party-reset')) party.socket?.send(JSON.stringify({ t: 'reset' }));
  if (t.closest('#copy-room')) navigator.clipboard?.writeText(`${location.origin}/party?room=${party.room.code}`).then(() => { t.textContent = 'copied ✓'; });
});
document.addEventListener('change', e => { if (e.target.matches('[data-setting]')) { settings[e.target.dataset.setting] = e.target.checked; write('sk:settings', settings); applySettings(); } if (e.target.id === 'party-name') savePartyName(); });
document.addEventListener('input', e => { if (e.target.matches('.search-answers')) { const text = e.target.value.toLowerCase(); e.target.nextElementSibling.querySelectorAll('li').forEach(li => li.hidden = !li.dataset.answer.includes(text)); } });
document.addEventListener('submit', e => {
  const id = e.target.id; if (!['suggestion-form', 'restore-form', 'name-form', 'friend-form', 'party-join-form', 'party-form', 'party-name-form'].includes(id)) return; e.preventDefault();
  if (id === 'suggestion-form') { const button = e.target.querySelector('button'); button.disabled = true; api('feedback', { kind: 'suggestion', text: $('suggestion').value }).then(() => { $('suggestion-status').textContent = 'Thanks — noted.'; $('suggestion').value = ''; }).catch(err => $('suggestion-status').textContent = err.message).finally(() => button.disabled = false); }
  if (id === 'restore-form') api('restore', { code: $('restore-input').value }).then(() => { $('restore-status').textContent = 'Logbook restored on this device.'; logbookCache = null; write('sk:run', null); setTimeout(() => location.reload(), 700); }).catch(err => $('restore-status').textContent = err.message);
  if (id === 'name-form') api('me', { name: $('name-input').value }).then(openFriends).catch(() => { });
  if (id === 'friend-form') api('friends/add', { code: $('friend-input').value }).then(openFriends).catch(err => $('friend-status').textContent = err.message);
  if (id === 'party-join-form') joinRoom($('party-code').value);
  if (id === 'party-name-form') savePartyName();
  if (id === 'party-form') { const v = $('party-answer').value.trim(); if (v && party.socket) { party.socket.send(JSON.stringify({ t: 'answer', text: v })); $('party-answer').value = ''; } }
});
document.querySelectorAll('dialog').forEach(d => { d.addEventListener('close', () => void resumeGame()); d.addEventListener('click', e => { if (e.target === d) { const r = d.getBoundingClientRect(); if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) d.close(); } }); });
$('export-history').addEventListener('click', async () => { const log = await loadLogbook(); const blob = new Blob([JSON.stringify({ server: log, local: read('sk:history', {}) }, null, 2)], { type: 'application/json' }), url = URL.createObjectURL(blob), a = document.createElement('a'); a.href = url; a.download = 'sourcekrillion-history.json'; a.click(); URL.revokeObjectURL(url); });
document.addEventListener('keydown', e => { if (e.key.toLowerCase() === 'f' && !['INPUT', 'TEXTAREA'].includes(document.activeElement?.tagName) && !e.ctrlKey && !e.metaKey) { if (document.fullscreenElement) document.exitFullscreen?.(); else $('game').requestFullscreen?.().catch(() => { }); } });
function viewport() { const height = window.visualViewport?.height || innerHeight; document.documentElement.style.setProperty('--vh', height + 'px'); } viewport(); window.visualViewport?.addEventListener('resize', viewport); window.addEventListener('resize', viewport);
window.render_game_to_text = () => JSON.stringify({ phase, prompt: game?.prompts[game.round]?.text || null, round: game?.round ?? 0, score: Math.round(visualScore), targetScore: currentScore, mode: game?.mode || 'daily', title: game?.title || null, results: game?.results.map(r => ({ answer: r.answer, tier: r.tier, score: r.score })) || [], party: party.room ? { status: party.room.status, holder: party.room.round?.holder, you: party.room.you } : null });
setInterval(tick, 80);
(async () => {
  try {
    today = await api('today'); $('day-number').textContent = 'DIVE #' + today.dayNumber;
    const saved = read('sk:run', null);
    if (saved?.runId && saved.date === today.date && saved.mode === 'daily') { try { const s = await api('state?run=' + encodeURIComponent(saved.runId)); { setGame(s); setHero(game.mode); if (game.pausedAt) setGame(await api('pause', { runId: game.runId, paused: false })); if (game.completed) await showResults(); else if (game.roundStart === null) showLanding(game.results.at(-1)); else showRound(); } } catch { write('sk:run', null); } }
  } catch (e) { $('boot-error').textContent = e.message; $('boot-error').hidden = false; }
  const view = document.body.dataset.view, params = new URLSearchParams(location.search);
  if (view === 'party') openParty(params.get('room'));
  else if (view === 'archive') openArchive(); else if (view === 'packs') openPacks(); else if (view === 'friends') openFriends(); else if (view === 'account') openLogbook(); else if (view === 'unlimited' && phase === 'intro') setHero('unlimited');
})();
