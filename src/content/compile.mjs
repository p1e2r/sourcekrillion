// Content format helpers.
//
// A prompt is written as  P(id, text, { tier: 'Answer/alias/alias | Answer2 | ...' })
// Tiers, most common to rarest: plankton, tooclever, schooler, rare, deepcut, krillion.
// "tooclever" is the "obscure" answer everybody reaches for; use it sparingly.
// Separate answers with "|" and give aliases after "/". Whitespace is trimmed.
import { TIERS, normalize } from '../../public/rules.mjs';

export function P(id, text, tiers) {
  const answers = [];
  for (const [tier, list] of Object.entries(tiers)) {
    if (!TIERS[tier]) throw new Error(`Unknown tier "${tier}" in ${id}`);
    for (const entry of list.split('|').map(s => s.trim()).filter(Boolean)) {
      const [answer, ...aliases] = entry.split('/').map(s => s.trim()).filter(Boolean);
      answers.push(aliases.length ? { answer, tier, aliases } : { answer, tier });
    }
  }
  return { id, text, answers };
}

export function validateBank(prompts, label) {
  const ids = new Set();
  for (const p of prompts) {
    if (ids.has(p.id)) throw new Error(`${label}: duplicate prompt id ${p.id}`);
    ids.add(p.id);
    if (p.answers.length < 4) throw new Error(`${label}: ${p.id} needs at least 4 answers`);
    const seen = new Map();
    for (const a of p.answers) for (const word of new Set([a.answer, ...(a.aliases || [])])) {
      const key = normalize(word);
      if (!key) throw new Error(`${label}: ${p.id} has an empty answer`);
      if (seen.has(key) && seen.get(key) !== a) throw new Error(`${label}: ${p.id} "${word}" is listed for both ${seen.get(key).answer} and ${a.answer}`);
      seen.set(key, a);
    }
  }
  return prompts;
}
