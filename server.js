import express from 'express';
import Database from 'better-sqlite3';
import path from 'path';
import { fileURLToPath } from 'url';
import dotenv from 'dotenv';

dotenv.config();

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
app.use(express.json());
app.set('view engine', 'ejs');
app.set('views', path.join(__dirname, 'views'));
app.use(express.static(path.join(__dirname, 'public')));

// Initialize SQLite Database
const db = new Database(path.join(__dirname, 'stats.db'));

// ===============================
// DB INIT & MIGRATION
// ===============================
db.exec(`
  CREATE TABLE IF NOT EXISTS stats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    server TEXT DEFAULT 'unknown',
    cpu REAL,
    ram REAL,
    disk REAL,
    inode REAL
  );
`);

// Ensure 'server' column exists (migration check)
try {
  db.prepare(`SELECT server FROM stats LIMIT 1`).get();
} catch (e) {
  db.exec(`ALTER TABLE stats ADD COLUMN server TEXT DEFAULT 'unknown';`);
  console.log("🔥 Added missing 'server' column to stats table");
}

// ===============================
// HELPERS
// ===============================
function health(cpu, ram, disk) {
  if (cpu > 90 || ram > 95 || disk > 90) return "critical";
  if (cpu > 70 || ram > 80 || disk > 80) return "warning";
  return "ok";
}

const insertStat = db.prepare(`
  INSERT INTO stats (timestamp, server, cpu, ram, disk, inode)
  VALUES (?, ?, ?, ?, ?, ?)
`);

const deleteOld = db.prepare(`DELETE FROM stats WHERE timestamp < ?`);

// ===============================
// ROUTES
// ===============================

// 1. RECEIVE STATS (From your servers)
app.post('/system-stats', (req, res) => {
  const { server, cpu, ram, disk, inode } = req.body;
  const now = Date.now(); // Storing ms usually better for JS dates
  const serverName = server || 'unknown';

  try {
    const tx = db.transaction(() => {
      insertStat.run(now, serverName, cpu, ram, disk, inode);
      // Keep only last 24 hours (86400000 ms)
      deleteOld.run(now - 86400000);
    });
    tx();
    console.log(`[DATA] Received from ${serverName}`);
    res.sendStatus(204);
  } catch (err) {
    console.error("❌ DB Insert Error:", err);
    res.sendStatus(500);
  }
});

// 2. DELETE SERVER (Purge API)
app.post('/delete-server', (req, res) => {
  const { server } = req.body;
  if (!server) return res.status(400).json({ error: "Server name required" });

  try {
    const info = db.prepare('DELETE FROM stats WHERE server = ?').run(server);
    console.log(`[SYSTEM] PURGED server: ${server} (${info.changes} records)`);
    res.json({ success: true });
  } catch (error) {
    console.error("Delete Error:", error);
    res.status(500).json({ error: "Database error" });
  }
});

// 3. FETCH LIVE DATA (JSON)
app.get('/live-data', (req, res) => {
  const server = req.query.server || null;

  if (server) {
    // Get history for one server
    const rows = db.prepare(`
      SELECT * FROM stats WHERE server=? ORDER BY timestamp DESC LIMIT 60
    `).all(server);
    return res.json({ mode: "single", rows });
  }

  // Get latest status for ALL servers
  const rows = db.prepare(`
    SELECT s.* FROM stats s
    INNER JOIN (
      SELECT server, MAX(timestamp) AS mt
      FROM stats GROUP BY server
    ) t ON s.server = t.server AND s.timestamp = t.mt
    ORDER BY s.server ASC
  `).all();

  res.json({ mode: "all", rows });
});

// 4. MAIN DASHBOARD UI
app.get("/", (req, res) => {
  const servers = db.prepare(`SELECT DISTINCT server FROM stats ORDER BY server ASC`).all().map(x => x.server);
  res.render("index", { servers });
});

// 5. FAVICON STATUS
app.get("/favicon.ico", (req, res) => {
  const latest = db.prepare(`SELECT cpu, ram, disk FROM stats ORDER BY timestamp DESC LIMIT 1`).get();
  let color = "🔴"; // No data
  if (latest) {
    const h = health(latest.cpu, latest.ram, latest.disk);
    if (h === "ok") color = "🟢";
    if (h === "warning") color = "🟡";
  }
  res.setHeader('Content-Type', 'image/svg+xml');
  res.send(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><circle cx="50" cy="50" r="50" fill="${color === '🟢' ? '#0f0' : (color === '🟡' ? '#ff0' : '#f00')}" /></svg>`);
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => console.log(`🚀 TERMINAL ONLINE: http://localhost:${PORT}`));