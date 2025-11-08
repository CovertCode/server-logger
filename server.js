import express from 'express';
import Database from 'better-sqlite3';
import path from 'path';
import { fileURLToPath } from 'url';
import dotenv from 'dotenv';

// Load .env
dotenv.config();

// Setup paths
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Express setup
const app = express();
app.use(express.json());
app.set('view engine', 'ejs');
app.set('views', path.join(__dirname, 'views'));

// Database setup
const db = new Database(path.join(__dirname, 'stats.db'));

// Create table if not exists
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

// Prepared statements
const insertStat = db.prepare(`
  INSERT INTO stats (timestamp, cpu, ram, disk, inode)
  VALUES (?, ?, ?, ?, ?)
`);
const deleteOld = db.prepare(`DELETE FROM stats WHERE timestamp < ?`);

// POST endpoint for system stats
app.post('/system-stats', (req, res) => {
  const { cpu, ram, disk, inode } = req.body;
  const now = Math.floor(Date.now() / 1000);

  try {
    insertStat.run(now, cpu, ram, disk, inode);
    deleteOld.run(now - 3600 * 24); // keep 24h data
    res.sendStatus(204);
  } catch (e) {
    console.error('DB insert error:', e);
    res.sendStatus(500);
  }
});

// View stats from last hour
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

// Start server
const PORT = process.env.PORT;
app.listen(PORT, () => {
  console.log(`🟢 Stats server running on http://localhost:${PORT}`);
});
