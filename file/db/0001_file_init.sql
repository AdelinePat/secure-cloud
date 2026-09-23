-- file service: owns files
-- Depends on auth's 0001_init.sql having already created users(id) — see run order in db/README.md

CREATE TABLE IF NOT EXISTS files (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id uuid,
  filename varchar NOT NULL,
  mime_type varchar,
  status varchar NOT NULL DEFAULT 'UPLOADING',
  size_bytes bigint NOT NULL,
  checksum varchar NOT NULL,
  storage_ref varchar NOT NULL,
  encryption_algorithm varchar NOT NULL DEFAULT 'AES-256-GCM',
  nonce varchar NOT NULL,
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  deleted_at timestamptz,

  CONSTRAINT fk_file_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL,
  CONSTRAINT chk_files_status CHECK (status IN ('UPLOADING', 'AVAILABLE', 'FAILED', 'DELETED'))
);
