-- auth service seed data
-- Passwords hashed with libsodium crypto_pwhash_str (Argon2id, OPSLIMIT/MEMLIMIT_INTERACTIVE).
-- Plaintext passwords (for manual login testing only): alice/password123, bob/password456, charlie/password789
-- Regenerate with tools/hash_seed.cpp if you add/change seed users.

INSERT INTO users (username, email, password_hash, public_key, key_algorithm)
VALUES
  ('alice',   'alice@example.com',   '$argon2id$v=19$m=65536,t=2,p=1$RnG8KJkBjgmzEElk6Jd2rA$WPtVe5OGoipvE+IfSNOss8LizrT9SXIs43IcGUkFEok',   'public-key-alice',   'X25519'),
  ('bob',     'bob@example.com',     '$argon2id$v=19$m=65536,t=2,p=1$KtqYyg/qkMYK0ildZAWF8g$O6+NGhD51IwSV5KbFyl5EKj2HbI9h4rZHAW7T5zZ4WA',   'public-key-bob',     'X25519'),
  ('charlie', 'charlie@example.com', '$argon2id$v=19$m=65536,t=2,p=1$//u+zOyczZ3Vbp/sbs3Aeg$07vnoCaq4Wyue8Y7cQQrJbnzW2qUngQ/xfLJSyinZE4', 'public-key-charlie', 'X25519')
ON CONFLICT (username) DO NOTHING;
