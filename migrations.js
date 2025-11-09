// migrate.js
import Database from "better-sqlite3";
import path from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const dbPath = path.join(__dirname, "stats.db");

console.log("🧩 Starting DB migration for", dbPath);

const db = new Database(dbPath);

// Helper: check if a column exists
function hasColumn(table, column) {
  const info = db.prepare(`PRAGMA table_info(${table})`).all();
  return info.some((c) => c.name === column);
}

// Helper: check if table exists
function hasTable(table) {
  const info = db
    .prepare(
      `SELECT name FROM sqlite_master WHERE type='table' AND name=?`
    )
    .get(table);
  return !!info;
}

// 1️⃣ Ensure main stats table
db.prepare(`
  CREATE TABLE IF NOT EXISTS stats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    cpu REAL,
    ram REAL,
    disk REAL,
    inode REAL
  )
`).run();

console.log("✅ Ensured 'stats' table exists.");

// 2️⃣ Add missing columns
if (!hasColumn("stats", "server")) {
  db.prepare(
    "ALTER TABLE stats ADD COLUMN server TEXT DEFAULT 'unknown'"
  ).run();
  console.log("🛠️ Added column: stats.server");
}

// 3️⃣ Create hourly aggregation table if missing
if (!hasTable("stats_hourly")) {
  db.prepare(`
    CREATE TABLE IF NOT EXISTS stats_hourly (
      hour TEXT NOT NULL,
      server TEXT NOT NULL,
      avg_cpu REAL,
      avg_ram REAL,
      avg_disk REAL,
      avg_inode REAL,
      PRIMARY KEY (hour, server)
    )
  `).run();
  console.log("✅ Created table: stats_hourly");
}

// 4️⃣ Apply performance pragmas
db.pragma("journal_mode = WAL");
db.pragma("synchronous = NORMAL");
console.log("⚙️  Enabled WAL mode for performance.");

// 5️⃣ Verify result
const schema = db
  .prepare("SELECT name, sql FROM sqlite_master WHERE type='table'")
  .all();
console.log("\n📋 Current schema:");
for (const s of schema) {
  console.log(`\n[${s.name}]\n${s.sql}`);
}

console.log("\n✅ Migration complete. You can now run: `node server.js`");
