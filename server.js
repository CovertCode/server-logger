import express from 'express';
import Database from 'better-sqlite3';
import path from 'path';
import { fileURLToPath } from 'url';
import dotenv from 'dotenv';

// Load .env file
dotenv.config();

// Setup paths
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Setup Express
const app = express();
app.use(express.json());
app.set('view engine', 'ejs');
app.set('views', path.join(__dirname, 'views'));

// Create or open SQLite database
const db = new Database(path.join(__dirname, 'stats.db'));

// Initialize table
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

// Prepare statements
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

// ===== Routes =====

// Receive stats from logger
app.post('/system-stats', (req, res) => {
  const { cpu, ram, disk, inode } = req.body;
  const now = Math.floor(Date.now() / 1000);

  console.log('Received payload:', req.body);

  try {
    insertStat.run(now, cpu, ram, disk, inode);
    deleteOld.run(now - 3600 * 24); // keep only 24h
    console.log('✅ Data written to SQLite:', { cpu, ram, disk, inode });
    res.sendStatus(204);
  } catch (err) {
    console.error('❌ Database write error:', err);
    res.sendStatus(500);
  }
});

// View data from the last hour
app.get('/', (req, res) => {
  const oneHourAgo = Math.floor(Date.now() / 1000) - 3600;
  const rows = db.prepare(`
    SELECT timestamp, cpu, ram, disk, inode
    FROM stats
    WHERE timestamp > ?
    ORDER BY timestamp ASC
  `).all(oneHourAgo);

  res.render('index', { rows });
});

// ===== Start server =====
const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`🟢 Stats server running on http://localhost:${PORT}`);
});
