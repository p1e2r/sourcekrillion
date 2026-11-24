-- SourceKrillion: more modes, players, friends, generated daily puzzles.
CREATE TABLE runs_v2 (
  id TEXT PRIMARY KEY,
  player TEXT NOT NULL,
  date TEXT NOT NULL,
  mode TEXT NOT NULL CHECK(mode IN ('daily','archive','unlimited','pack','practice')),
  puzzle TEXT NOT NULL,
  content TEXT,
  number INTEGER,
  next_round INTEGER NOT NULL DEFAULT 0,
  round_start INTEGER,
  completed INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  paused_at INTEGER
);
INSERT INTO runs_v2(id,player,date,mode,puzzle,next_round,round_start,completed,created_at,paused_at)
  SELECT id,player,date,mode,date,next_round,round_start,completed,created_at,paused_at FROM runs;
DROP TABLE runs;
ALTER TABLE runs_v2 RENAME TO runs;
CREATE UNIQUE INDEX one_daily_per_player ON runs(player,date) WHERE mode='daily';
CREATE UNIQUE INDEX one_archive_per_player ON runs(player,puzzle) WHERE mode='archive';
CREATE INDEX daily_results ON runs(date,mode,completed);
CREATE INDEX player_runs ON runs(player,mode,completed);

CREATE TABLE players (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL DEFAULT '',
  friend_code TEXT NOT NULL UNIQUE,
  restore_hash TEXT UNIQUE,
  created_at INTEGER NOT NULL
);
CREATE TABLE friends (
  player TEXT NOT NULL,
  friend TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY(player,friend)
);
CREATE TABLE generation_log (
  date TEXT NOT NULL,
  attempt_at INTEGER NOT NULL,
  ok INTEGER NOT NULL,
  detail TEXT
);
