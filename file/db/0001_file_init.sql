-- file service: owns files, file_chunks
-- Depends on auth's init (users) and messaging's init (conversations), so it
-- must run after both — see the app-db init order in docker-compose.yml.
--
-- Files are end-to-end encrypted on the client (libsodium secretstream) and
-- uploaded as numbered ciphertext chunks. The server never sees the file key,
-- name or type: those travel inside the encrypted chat message that points
-- at files.id.

CREATE TABLE IF NOT EXISTS files (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  conversation_id uuid NOT NULL,          -- access = membership of this conversation
  user_id uuid,                           -- uploader
  status varchar NOT NULL DEFAULT 'UPLOADING',
  size_bytes bigint NOT NULL,             -- total ciphertext size announced at creation
  chunk_count integer NOT NULL,
  encryption_algorithm varchar NOT NULL DEFAULT 'xchacha20poly1305-secretstream',
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  completed_at timestamptz,               -- set when status becomes AVAILABLE
  deleted_at timestamptz,

  CONSTRAINT fk_file_conversation FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE,
  CONSTRAINT fk_file_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL,
  CONSTRAINT chk_files_status CHECK (status IN ('UPLOADING', 'AVAILABLE', 'FAILED', 'DELETED')),
  CONSTRAINT chk_files_size CHECK (size_bytes > 0),
  CONSTRAINT chk_files_chunk_count CHECK (chunk_count > 0)
);

-- One row per received chunk. The bytes live on disk at
-- <FILE_STORAGE_DIR>/<file_id>/<chunk_index>; re-uploading a chunk replaces it.
CREATE TABLE IF NOT EXISTS file_chunks (
  file_id uuid NOT NULL,
  chunk_index integer NOT NULL,
  size_bytes integer NOT NULL,
  sha256 char(64) NOT NULL,               -- hex digest of the stored ciphertext
  received_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,

  PRIMARY KEY (file_id, chunk_index),

  CONSTRAINT fk_chunk_file FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE,
  CONSTRAINT chk_chunk_index CHECK (chunk_index >= 0),
  CONSTRAINT chk_chunk_size CHECK (size_bytes > 0)
);

CREATE INDEX IF NOT EXISTS idx_files_conversation ON files (conversation_id);
-- For the cleanup of abandoned uploads (status = 'UPLOADING', old created_at).
CREATE INDEX IF NOT EXISTS idx_files_status_created ON files (status, created_at);
