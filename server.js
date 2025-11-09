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

const db = new Database(path.join(__dirname, 'stats.db'));

// Initialize table if not exists
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

const insertStat = db.prepare(`
  INSERT INTO stats (timestamp, server, cpu, ram, disk, inode)
  VALUES (?, ?, ?, ?, ?, ?)
`);

const deleteOld = db.prepare(`
  DELETE FROM stats WHERE timestamp < ?
`);

// ===== Middleware for logging requests =====
app.use((req, res, next) => {
  const now = new Date().toISOString();
  console.log(`[${now}] ${req.method} ${req.url}`);
  next();
});

// ===== API Endpoint =====
app.post('/system-stats', (req, res) => {
  const { server, cpu, ram, disk, inode } = req.body;
  const now = Math.floor(Date.now() / 1000);

  console.log(`[${new Date().toISOString()}] Received payload:`, req.body);

  try {
    const serverName = server || 'unknown'; // fallback, just in case
    db.transaction(() => {
      insertStat.run(now, serverName, cpu, ram, disk, inode);
      deleteOld.run(now - 3600 * 24);
    })();
    console.log(`✅ [${serverName}] Data written to SQLite:`, { cpu, ram, disk, inode });
    res.sendStatus(204);
  } catch (err) {
    console.error('❌ Database write error:', err);
    res.sendStatus(500);
  }
});


// ===== View Route =====
app.get('/', (req, res) => {
  const serverFilter = req.query.server || '';
  const query = serverFilter
    ? `SELECT * FROM stats WHERE server = ? ORDER BY timestamp DESC LIMIT 200`
    : `SELECT * FROM stats ORDER BY timestamp DESC LIMIT 200`;

  const rows = serverFilter
    ? db.prepare(query).all(serverFilter)
    : db.prepare(query).all();

  // compute averages of last 20 for current filter
  const last20 = rows.slice(0, 20);
  const avg = { cpu: 0, ram: 0, disk: 0, inode: 0 };
  if (last20.length) {
    for (const r of last20) {
      avg.cpu += r.cpu;
      avg.ram += r.ram;
      avg.disk += r.disk;
      avg.inode += r.inode;
    }
    for (const k in avg) avg[k] /= last20.length;
  }

  // get distinct servers for dropdown
  const servers = db.prepare(`SELECT DISTINCT server FROM stats ORDER BY server`).all();

  res.render('index', { rows, avg, servers, currentServer: serverFilter });
});

// =========================================================
// Secure endpoint: Clear all SQLite stats (with separate key)
// =========================================================
app.get('/clear-stats', (req, res) => {
  const apiKey = req.query.api_key;

  if (!apiKey || apiKey !== process.env.CLEAR_KEY) {
    console.warn(`[SECURITY] Unauthorized clear-stats attempt from ${req.ip}`);
    return res.status(401).send('Unauthorized: Invalid API key');
  }

  try {
    const deletedStats = db.prepare('DELETE FROM stats').run();
    const deletedHourly = db.prepare('DELETE FROM stats_hourly').run();
    console.log(
      `🧹 Cleared all data: ${deletedStats.changes} rows (stats), ${deletedHourly.changes} rows (hourly)`
    );
    res.send(
      `✅ All stats cleared.<br>Deleted ${deletedStats.changes} rows from stats, ${deletedHourly.changes} rows from stats_hourly.`
    );
  } catch (err) {
    console.error('❌ Error clearing stats:', err);
    res.status(500).send('Error clearing stats');
  }
});


const PORT = process.env.PORT || 3000;
app.listen(PORT, () =>
  console.log(`🟢 Stats server running on http://localhost:${PORT}`)
);
