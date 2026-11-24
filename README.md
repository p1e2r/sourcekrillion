# SourceKrillion 🦐

An independent, open-source rare-answer trivia game. Seven prompts, 25 seconds each. Familiar answers float; rare answers sink deeper.

SourceKrillion is a fork of the MIT-licensed [OpenKrillion](https://github.com/VulcanEon/openkrillion.com-service), reworked to play more like the original Krillion while keeping OpenKrillion's art. It is **not affiliated with krillion.io** and uses none of its content.

## What's in it

- **Daily dive:** seven new prompts every day (New York midnight), the same for everyone. They're written by a free AI model (Groq) in a strict format, checked, and frozen once played. If generation fails, the day comes from the built-in classic pool.
- **∞ Unlimited:** seven fresh prompts per dive, dive after dive, numbered per player.
- **⟲ Archive:** replay any past daily dive. Replays don't extend your streak.
- **▤ Themed packs:** Words & Language, At the Movies, Game On and Passport Ready, made of chapters that unlock in order.
- **☻ Friends:** friend codes, display names and a daily leaderboard. You only see friends' scores after you finish your own dive.
- **My SourceKrillion:** a logbook stored on the server with streaks and history. Use a restore code to move it to another device.
- **◉ Puffer Party:** live multiplayer. Everyone gets the same category, the holder names a new valid answer to pass the puffer, it pops at a random moment, and the last one floating wins. Public rooms and private room codes.
- **Native app:** a C++ desktop app for macOS and Windows that looks and plays like the web version, with offline play (see [native/README.md](native/README.md)).

## Run it with Docker

```sh
cp .env.example .env        # fill in GROQ_API_KEY (and DEEP_WORD if you want one)
docker compose up -d        # http://localhost:3051
```

`docker-compose.yml` uses `ghcr.io/p1e2r/sourcekrillion:latest`; change it to your GitHub user. Game data (SQLite) lives in `./data`.

| Variable | Purpose |
|---|---|
| `GROQ_API_KEY` | Free key from [console.groq.com](https://console.groq.com). Enables the AI daily puzzle. Without it, the classic pool is used. |
| `GROQ_MODEL` | Defaults to `openai/gpt-oss-120b`. |
| `DEEP_WORD` | Secret word: typing it as an answer always scores the deepest tier (One in a Krillion, 100). It only exists in your server's environment and is off when empty. |
| `LAUNCH_DATE` | Day 1 of the game and the start of the archive (`YYYY-MM-DD`). |
| `ADMIN_TOKEN` | Optional. `POST /api/admin/generate {"date":"YYYY-MM-DD"}` with `Authorization: Bearer …` generates a day by hand. |
| `PORT`, `DATABASE_PATH` | Default `3051` and `/data/sourcekrillion.db`. |

**Keep `.env` out of Git.** It's in `.gitignore`, and the image never contains it. `DEEP_WORD` is only read from the environment, so the word itself is never in the code, the image or the repository.

### How the daily AI puzzle stays inside the free tier

Groq's free plan allows about 30 requests and 8,000 tokens per minute and 1,000 requests and 200,000 tokens per day for `gpt-oss-120b`. The server asks for **one prompt per request**, 40 seconds apart, so each day takes about 7–11 requests and roughly 30k tokens. It prepares tomorrow's puzzle ahead of time and retries at most three times every six hours.

## Publish the image to GHCR

Push this repository to GitHub. `.github/workflows/docker.yml` runs the tests, then builds `linux/amd64` and `linux/arm64` images and pushes them to `ghcr.io/<owner>/sourcekrillion` (`latest` on `main`, plus version tags for `v*` tags). `native.yml` builds the macOS (universal) and Windows apps and attaches them to releases.

## Develop

```sh
npm ci
npm run build     # pixel sprites, font, icons
npm test          # server, content, generator and party tests
npm run dev       # http://localhost:3051 (database in ./data/dev.db)
```

Node 22.13+ is needed (it uses the built-in `node:sqlite`).

### Adding content

- Classic pool: `src/content/classic.mjs`
- Packs: `src/content/packs.mjs` (append chapters of exactly seven prompts)

Each prompt is written as tiers of `Answer/alias/alias` separated by `|`:

```js
P('c-planets', 'Name a planet', { plankton: 'Mars | Earth', schooler: 'Saturn | Venus', rare: 'Mercury', krillion: 'Neptune/Planet Neptune' })
```

Tiers from most common to rarest: `plankton` 10, `tooclever` 15, `schooler` 30, `rare` 60, `deepcut` 85, `krillion` 100. The checker refuses duplicate or conflicting answers.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
