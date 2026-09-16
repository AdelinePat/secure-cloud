CREATE TABLE "users" (
  "id" uuid PRIMARY KEY DEFAULT (gen_random_uuid()),
  "username" varchar UNIQUE NOT NULL,
  "email" varchar UNIQUE NOT NULL,
  "password" varchar NOT NULL,
  "public_key" varchar NOT NULL,
  "created_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  "updated_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP)
);

CREATE TABLE "sessions" (
  "id" uuid PRIMARY KEY DEFAULT (gen_random_uuid()),
  "user_id" uuid NOT NULL,
  "refresh_token_hash" varchar NOT NULL,
  "issued_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  "expires_at" timestamptz NOT NULL,
  "revoked_at" timestamptz
);

CREATE TABLE "conversations" (
  "id" uuid PRIMARY KEY DEFAULT (gen_random_uuid()),
  "creator_id" uuid, -- nullable: SET NULL on delete,
  "type" varchar NOT NULL,
  "name" varchar NOT NULL,
  "created_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  "updated_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP)
);

CREATE TABLE "conversation_participants" (
  "conversation_id" uuid NOT NULL,
  "user_id" uuid NOT NULL,
  "role" varchar NOT NULL DEFAULT 'member',
  "joined_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  PRIMARY KEY ("conversation_id", "user_id")
);

CREATE TABLE "messages" (
  "id" uuid PRIMARY KEY DEFAULT (gen_random_uuid()),
  "conversation_id" uuid NOT NULL,
  "sender_id" uuid, -- nullable: SET NULL on delete,
  "ciphertext" bytea NOT NULL,
  "nonce" varchar NOT NULL, -- required for AEAD decryption
  "server_timestamp" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  "updated_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  "deleted_at" timestamptz
);

CREATE TABLE "message_attachments" (
  "message_id" uuid NOT NULL,
  "file_id" uuid NOT NULL,
  PRIMARY KEY ("message_id", "file_id")
);

CREATE TABLE "files" (
  "id" uuid PRIMARY KEY DEFAULT (gen_random_uuid()),
  "user_id" uuid, -- nullable: SET NULL on delete,
  "conversation_id" uuid,
  "filename" varchar NOT NULL,
  "mime_type" varchar,
  "status" varchar,
  "size_bytes" bigint NOT NULL,
  "checksum" varchar NOT NULL,
  "storage_ref" varchar NOT NULL,
  "encryption_algorithm" varchar NOT NULL DEFAULT 'AES-256-GCM', -- keep an authenticated mode;
  "nonce" varchar NOT NULL,
  "created_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  "deleted_at" timestamptz
);

-- Nice to have

CREATE TABLE "message_delivery_receipts" (
  "message_id" uuid NOT NULL,
  "user_id" uuid NOT NULL,
  "delivered_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  PRIMARY KEY ("message_id", "user_id")
);

CREATE TABLE "message_read_receipts" (
  "message_id" uuid NOT NULL,
  "user_id" uuid NOT NULL,
  "read_at" timestamptz NOT NULL DEFAULT (CURRENT_TIMESTAMP),
  PRIMARY KEY ("message_id", "user_id")
);

CREATE TABLE "presence" (
  "user_id" uuid PRIMARY KEY,
  "status" varchar DEFAULT 'offline',
  "last_seen_at" timestamptz
);

CREATE INDEX ON "messages" ("conversation_id", "server_timestamp");

CREATE INDEX ON "files" ("user_id", "conversation_id");

-- Foreign keys, with ON DELETE actions

ALTER TABLE "sessions" ADD FOREIGN KEY ("user_id")
  REFERENCES "users" ("id") ON DELETE CASCADE;

ALTER TABLE "conversations" ADD FOREIGN KEY ("creator_id")
  REFERENCES "users" ("id") ON DELETE SET NULL;

ALTER TABLE "conversation_participants" ADD FOREIGN KEY ("conversation_id")
  REFERENCES "conversations" ("id") ON DELETE CASCADE;

ALTER TABLE "conversation_participants" ADD FOREIGN KEY ("user_id")
  REFERENCES "users" ("id") ON DELETE CASCADE;

ALTER TABLE "messages" ADD FOREIGN KEY ("conversation_id")
  REFERENCES "conversations" ("id") ON DELETE CASCADE;

ALTER TABLE "messages" ADD FOREIGN KEY ("sender_id")
  REFERENCES "users" ("id") ON DELETE SET NULL;

ALTER TABLE "message_attachments" ADD FOREIGN KEY ("message_id")
  REFERENCES "messages" ("id") ON DELETE CASCADE;

ALTER TABLE "message_attachments" ADD FOREIGN KEY ("file_id") -- was missing entirely
  REFERENCES "files" ("id") ON DELETE CASCADE;

ALTER TABLE "files" ADD FOREIGN KEY ("user_id")
  REFERENCES "users" ("id") ON DELETE SET NULL;

ALTER TABLE "files" ADD FOREIGN KEY ("conversation_id")
  REFERENCES "conversations" ("id") ON DELETE SET NULL;

ALTER TABLE "message_delivery_receipts" ADD FOREIGN KEY ("message_id")
  REFERENCES "messages" ("id") ON DELETE CASCADE;

ALTER TABLE "message_delivery_receipts" ADD FOREIGN KEY ("user_id")
  REFERENCES "users" ("id") ON DELETE CASCADE;

ALTER TABLE "message_read_receipts" ADD FOREIGN KEY ("message_id")
  REFERENCES "messages" ("id") ON DELETE CASCADE;

ALTER TABLE "message_read_receipts" ADD FOREIGN KEY ("user_id")
  REFERENCES "users" ("id") ON DELETE CASCADE;

ALTER TABLE "presence" ADD FOREIGN KEY ("user_id")
  REFERENCES "users" ("id") ON DELETE CASCADE;

-- Optional: auto-maintain updated_at on UPDATE (Postgres does not do this by default —
-- the DEFAULT above only fires on INSERT).

CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = CURRENT_TIMESTAMP;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_users_updated_at
  BEFORE UPDATE ON "users"
  FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER trg_conversations_updated_at
  BEFORE UPDATE ON "conversations"
  FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER trg_messages_updated_at
  BEFORE UPDATE ON "messages"
  FOR EACH ROW EXECUTE FUNCTION set_updated_at();
