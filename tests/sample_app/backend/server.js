const express = require('express');
const { Pool } = require('pg');
const cors = require('cors');

const app = express();
const port = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());

// The HTTP_Server K8s controller sets environment variables dynamically.
// For a DB with alias "main", it injects MAIN_DB_URL.
const pool = new Pool({
  connectionString: process.env.MAIN_DB_URL || 'postgresql://postgres:postgres@localhost:5432/sample_db'
});

app.get('/api/status', async (req, res) => {
  try {
    const client = await pool.connect();
    const result = await client.query('SELECT NOW() as time');
    client.release();
    res.json({ 
        success: true, 
        message: "Backend is running and connected to the database!",
        db_time: result.rows[0].time 
    });
  } catch (err) {
    res.status(500).json({ 
        success: false, 
        message: "Backend is running, but failed to connect to the database.",
        error: err.message 
    });
  }
});

app.listen(port, () => {
  console.log(`Sample backend listening on port ${port}`);
});
