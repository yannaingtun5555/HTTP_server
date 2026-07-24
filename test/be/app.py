"""
test-site/be/app.py
Minimal Flask backend for the HTTP Server end-to-end test site.

Endpoints:
  GET /api/hello      — health check / greeting
  GET /api/db-check   — queries PostgreSQL and returns the server timestamp
"""

import os
import datetime
import psycopg2
from flask import Flask, jsonify

app = Flask(__name__)

# ── Database connection string (injected by docker-compose / admin panel) ──
DB_URL = os.environ.get("DATABASE_URL", "")


def get_db_conn():
    """Open a fresh connection using DATABASE_URL env var."""
    if not DB_URL:
        raise RuntimeError("DATABASE_URL is not set")
    return psycopg2.connect(DB_URL)


# ── Routes ────────────────────────────────────────────────────

@app.route("/api/hello")
def hello():
    return jsonify({
        "ok": True,
        "message": "Hello from the Test Backend!",
        "backend": "Flask",
        "timestamp": datetime.datetime.utcnow().isoformat() + "Z",
    })


@app.route("/api/db-check")
def db_check():
    try:
        conn = get_db_conn()
        cur = conn.cursor()
        cur.execute("SELECT NOW()::TEXT, current_database(), version()")
        row = cur.fetchone()
        cur.close()
        conn.close()
        return jsonify({
            "ok": True,
            "server_time": row[0],
            "database": row[1],
            "version": row[2],
        })
    except Exception as exc:
        return jsonify({
            "ok": False,
            "error": str(exc),
        }), 500


@app.route("/api/healthz")
def healthz():
    return jsonify({"status": "ok"})


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5000))
    app.run(host="0.0.0.0", port=port)
