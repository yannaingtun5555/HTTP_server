-- ============================================================
--  schema.sql — Server database schema
--  Run once on first boot; all statements use IF NOT EXISTS.
-- ============================================================

CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ── domains ──────────────────────────────────────────────────
-- One row per managed website / domain
CREATE TABLE IF NOT EXISTS domains (
    id          SERIAL PRIMARY KEY,
    domain      TEXT UNIQUE NOT NULL,
    user_owner  TEXT,
    fe_folder   TEXT,
    be_folder   TEXT,
    be_type     TEXT,          -- node | python | go | java | php | static
    run_cmd     TEXT,
    be_port     INT,
    fe_build    TEXT,          -- npm | vite | next | hugo | none
    db_count    INT DEFAULT 0,
    status      TEXT DEFAULT 'pending',   -- pending | building | running | error
    error_msg   TEXT,
    created_at  TIMESTAMPTZ DEFAULT NOW(),
    updated_at  TIMESTAMPTZ DEFAULT NOW()
);

-- ── pages ─────────────────────────────────────────────────────
-- Static page metadata for each domain (populated after FE build)
CREATE TABLE IF NOT EXISTS pages (
    id           SERIAL PRIMARY KEY,
    domain_id    INT NOT NULL REFERENCES domains(id) ON DELETE CASCADE,
    path         TEXT NOT NULL,            -- e.g. /about, /index.html
    content_hash TEXT,                     -- SHA-256 of file content
    size_bytes   BIGINT,
    built_at     TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE (domain_id, path)
);

-- ── db_containers ─────────────────────────────────────────────
-- One row per DB container per domain (a domain can have 1..N DBs)
CREATE TABLE IF NOT EXISTS db_containers (
    id              SERIAL PRIMARY KEY,
    domain_id       INT NOT NULL REFERENCES domains(id) ON DELETE CASCADE,
    db_alias        TEXT NOT NULL,         -- logical name from sites.conf
    db_type         TEXT NOT NULL,         -- postgres | mysql | mongo | redis
    db_name         TEXT NOT NULL,
    k8s_namespace   TEXT,
    k8s_deployment  TEXT,
    k8s_service     TEXT,
    cluster_ip      TEXT,
    port            INT,
    connection_str  TEXT,                  -- generated after pod Running
    status          TEXT DEFAULT 'pending',-- pending | running | error
    error_msg       TEXT,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ── be_containers ─────────────────────────────────────────────
-- One row per BE container per domain
CREATE TABLE IF NOT EXISTS be_containers (
    id              SERIAL PRIMARY KEY,
    domain_id       INT NOT NULL REFERENCES domains(id) ON DELETE CASCADE,
    image_tag       TEXT,
    k8s_namespace   TEXT,
    k8s_deployment  TEXT,
    k8s_service     TEXT,
    be_host         TEXT,                  -- ClusterIP of the service
    be_port         INT,
    status          TEXT DEFAULT 'pending',-- pending | running | error
    error_log       TEXT,                  -- last N bytes of stderr on failure
    deployed_at     TIMESTAMPTZ,
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ── Indexes ───────────────────────────────────────────────────
CREATE INDEX IF NOT EXISTS idx_pages_domain       ON pages(domain_id);
CREATE INDEX IF NOT EXISTS idx_db_containers_dom  ON db_containers(domain_id);
CREATE INDEX IF NOT EXISTS idx_be_containers_dom  ON be_containers(domain_id);

-- ── Updated-at trigger ────────────────────────────────────────
CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$;

DO $$ BEGIN
    CREATE TRIGGER trg_domains_upd
        BEFORE UPDATE ON domains
        FOR EACH ROW EXECUTE FUNCTION set_updated_at();
EXCEPTION WHEN duplicate_object THEN NULL; END $$;

DO $$ BEGIN
    CREATE TRIGGER trg_db_containers_upd
        BEFORE UPDATE ON db_containers
        FOR EACH ROW EXECUTE FUNCTION set_updated_at();
EXCEPTION WHEN duplicate_object THEN NULL; END $$;

DO $$ BEGIN
    CREATE TRIGGER trg_be_containers_upd
        BEFORE UPDATE ON be_containers
        FOR EACH ROW EXECUTE FUNCTION set_updated_at();
EXCEPTION WHEN duplicate_object THEN NULL; END $$;
