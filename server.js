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
  INSERT INTO stats (timestamp, cpu, ram, disk, inode)
  VALUES (?, ?, ?, ?, ?)
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
  const { cpu, ram, disk, inode } = req.body;
  const now = Math.floor(Date.now() / 1000);

  console.log('Received payload:', req.body);

  try {
    insertStat.run(now, cpu, ram, disk, inode);
    deleteOld.run(now - 3600 * 24);
    console.log('✅ Data written to SQLite:', { cpu, ram, disk, inode });
    res.sendStatus(204);
  } catch (err) {
    console.error('❌ Database write error:', err);
    res.sendStatus(500);
  }
});

// ===== View Route =====
app.get('/', (req, res) => {
  // Get recent entries (latest first)
  const rows = db.prepare(`
    SELECT timestamp, cpu, ram, disk, inode
    FROM stats
    ORDER BY timestamp DESC
    LIMIT 200
  `).all();

  // Compute averages of last 20 entries
  const recent20 = rows.slice(0, 20);
  const avg = {
    cpu: 0,
    ram: 0,
    disk: 0,
    inode: 0,
  };
  if (recent20.length > 0) {
    for (const r of recent20) {
      avg.cpu += r.cpu;
      avg.ram += r.ram;
      avg.disk += r.disk;
      avg.inode += r.inode;
    }
    avg.cpu /= recent20.length;
    avg.ram /= recent20.length;
    avg.disk /= recent20.length;
    avg.inode /= recent20.length;
  }

  res.render('index', { rows, avg });
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () =>
  console.log(`🟢 Stats server running on http://localhost:${PORT}`)
);
