-- messaging service: owns conversations, conversation_participants, messages, message_attachments
-- Depends on auth's 0001_init.sql having already created users(id) — see run order in db/README.md

CREATE TABLE IF NOT EXISTS conversations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  creator_id uuid,
  type varchar NOT NULL,
  name varchar, -- NULL for type='direct'; required for type='group'
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,

  CONSTRAINT fk_conversations_creator FOREIGN KEY (creator_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS conversation_participants (
  conversation_id uuid NOT NULL,
  user_id uuid NOT NULL,
  role varchar NOT NULL,
  joined_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,

  PRIMARY KEY (conversation_id, user_id),

  CONSTRAINT fk_participant_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
  CONSTRAINT fk_participant_conversation FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS messages (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  conversation_id uuid NOT NULL,
  user_id uuid,
  ciphertext text NOT NULL,
  nonce text NOT NULL,
  encryption_version int NOT NULL DEFAULT 1,
  server_timestamp timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  client_timestamp timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  deleted_at timestamptz,

  CONSTRAINT fk_message_conversation FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE,
  CONSTRAINT fk_message_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS message_attachments (
  message_id uuid NOT NULL,
  file_id uuid NOT NULL,

  PRIMARY KEY (message_id, file_id),

  CONSTRAINT fk_attachment_message FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_message_conversation ON messages (conversation_id, server_timestamp);
CREATE INDEX IF NOT EXISTS idx_conversation_participants_user ON conversation_participants(user_id);
CREATE INDEX IF NOT EXISTS idx_conversation_participants_conversation ON conversation_participants(conversation_id);
CREATE INDEX IF NOT EXISTS idx_message_attachments_file ON message_attachments(file_id);

DROP TRIGGER IF EXISTS trg_conversations_updated_at ON conversations;
CREATE TRIGGER trg_conversations_updated_at
  BEFORE UPDATE ON conversations
  FOR EACH ROW EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_messages_updated_at ON messages;
CREATE TRIGGER trg_messages_updated_at
  BEFORE UPDATE ON messages
  FOR EACH ROW EXECUTE FUNCTION set_updated_at();
