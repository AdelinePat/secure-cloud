# File API (protected)

End-to-end encrypted file sharing. The client encrypts the file, uploads it
as numbered ciphertext **chunks**, then sends the file's key to the other
participants **inside an ordinary encrypted chat message**. The server only
stores ciphertext: it never sees the key, the file name or its type.

- **Public URL:** through the gateway, same paths: `http://<gateway>:8080/files/...`
  (the gateway forwards `/files` and `/files/...` unchanged to the file
  service on port 8083).
- **Auth:** every route needs `Authorization: Bearer <access token>` (the
  JWT from `POST /auth/login`).
- **Access:** a file belongs to the conversation it was created for. Only
  participants of that conversation can see or download it. Only the
  uploader can send chunks or complete the upload. The uploader or a
  conversation admin can delete it.
- **Errors** are `{"error": "CODE"}`, like the Messages API.

| Method   | Endpoint                      | Role                                          |
| -------- | ----------------------------- | --------------------------------------------- |
| `POST`   | `/files`                      | Start an upload                               |
| `PUT`    | `/files/{fileId}/chunks/{n}`  | Upload ciphertext chunk `n`                   |
| `GET`    | `/files/{fileId}`             | Metadata (+ chunks received, while uploading) |
| `POST`   | `/files/{fileId}/complete`    | Finish the upload                             |
| `GET`    | `/files/{fileId}/chunks/{n}`  | Download ciphertext chunk `n`                 |
| `DELETE` | `/files/{fileId}`             | Delete the file                               |

## Limits

| Limit                         | Value                                                     |
| ----------------------------- | --------------------------------------------------------- |
| Chunk size (ciphertext)       | 1 to 1 048 593 bytes (1 MiB plaintext + 17 bytes of encryption overhead) |
| Request body (gateway + file) | 2 MiB; above that: `413 PAYLOAD_TOO_LARGE`                |
| File size (ciphertext)        | 2 GiB                                                     |
| `chunk_count`                 | `1 ≤ chunk_count ≤ size_bytes ≤ chunk_count × 1 048 593`  |

## Errors common to every route

| Code | `error`                     | When                                                         |
| ---- | --------------------------- | ------------------------------------------------------------ |
| 401  | `UNAUTHORIZED`              | missing, invalid or expired token                            |
| 403  | `FORBIDDEN`                 | not a participant of the file's conversation, or not allowed for this action (logged to audit) |
| 404  | `FILE_NOT_FOUND`            | unknown or deleted file, or `fileId` is not a UUID            |
| 404  | `NOT_FOUND`                 | unknown route                                                |
| 405  | `METHOD_NOT_ALLOWED`        | known route, wrong method                                    |
| 413  | `PAYLOAD_TOO_LARGE`         | request body over 2 MiB                                      |
| 500  | `INTERNAL_ERROR`            | unexpected server error                                      |
| 502  | `FILE_SERVICE_UNAVAILABLE`  | (gateway) the file service cannot be reached                 |

## The file object

Returned by `POST /files`, `GET /files/{id}` and `POST /files/{id}/complete`.

```json
{
  "file_id": "3bae935a-a384-4e10-bfd6-21295d82cac3",
  "conversation_id": "22222222-2222-4222-8222-222222222222",
  "uploader_id": "aaaaaaaa-0000-4000-8000-000000000001",
  "status": "AVAILABLE",
  "size_bytes": 1548593,
  "chunk_count": 2,
  "encryption_algorithm": "xchacha20poly1305-secretstream",
  "created_at": "2026-11-25 10:33:57.228721+00",
  "completed_at": "2026-11-25 10:34:21.669876+00"
}
```

- `status`: `UPLOADING` → `AVAILABLE` (after `complete`). Deleted files are
  no longer returned (404).
- `size_bytes`: total **ciphertext** size (sum of all chunks).
- `uploader_id`: `null` if that user account was deleted.
- `completed_at`: `null` until the upload is complete.
- Timestamps are Postgres text timestamps (UTC), as in the Messages API.

---

## `POST /files` start an upload

Headers:

```jsx
{
 "Content-Type": "application/json",
 "Authorization": "Bearer eyJhbGciOi..."
}
```

Body:

```json
{
  "conversation_id": "22222222-2222-4222-8222-222222222222",
  "size_bytes": 1548593,
  "chunk_count": 2
}
```

Response: `201 Created` with the file object (`status: "UPLOADING"`).

Errors:

| Code | `error`             | When                                                       |
| ---- | ------------------- | ---------------------------------------------------------- |
| 400  | `INVALID_FILE_DATA` | malformed JSON, missing field, `conversation_id` not a UUID, non-integer or non-positive numbers, `chunk_count` incompatible with `size_bytes` |
| 403  | `FORBIDDEN`         | caller is not a participant of the conversation (or it does not exist) |
| 413  | `FILE_TOO_LARGE`    | `size_bytes` over 2 GiB                                    |

---

## `PUT /files/{fileId}/chunks/{n}` upload one chunk

`n` goes from `0` to `chunk_count - 1`. Chunks can be sent in any order.
Sending a chunk again replaces it, so a client can simply retry after a
timeout or a dropped connection.

Headers:

```jsx
{
 "Content-Type": "application/octet-stream",
 "Authorization": "Bearer eyJhbGciOi..."
}
```

Body: the raw ciphertext bytes of chunk `n`.

Response: `200 OK`

```json
{
  "chunk_index": 0,
  "size_bytes": 1048593,
  "sha256": "cb6d74472462febcbd8b4b07992e0c7ae1a692cabecd3d17b4d13b336d9d61a2"
}
```

`sha256` is the hex digest of the bytes the server stored; the client can
compare it with its own to detect corruption in transit.

Errors:

| Code | `error`               | When                                                   |
| ---- | --------------------- | ------------------------------------------------------ |
| 400  | `INVALID_CHUNK_INDEX` | `n` is not a plain non-negative integer (`01`, `-1`, `x`...) or is ≥ `chunk_count` |
| 400  | `EMPTY_CHUNK`         | empty body                                             |
| 403  | `FORBIDDEN`           | caller is not the uploader                             |
| 409  | `FILE_NOT_UPLOADING`  | the upload is already complete                         |
| 413  | `CHUNK_TOO_LARGE`     | body over 1 048 593 bytes                              |

---

## `GET /files/{fileId}` get metadata

Any participant of the conversation.

Response: `200 OK` with the file object. While `status` is `UPLOADING`, it
also contains the chunks received so far, so an interrupted upload can
resume by sending only the missing ones:

```json
{
  "file_id": "3bae935a-...",
  "status": "UPLOADING",
  "chunk_count": 3,
  "received_chunks": [0, 2],
  "...": "other file fields"
}
```

Errors: only the common ones (401, 403, 404).

---

## `POST /files/{fileId}/complete` finish the upload

Uploader only, no body. The server checks that every chunk `0 … chunk_count-1`
was received and that their sizes add up to `size_bytes`, then sets the file
to `AVAILABLE`. Calling it again on an available file returns `200` again,
so retrying after a lost response is safe.

Response: `200 OK` with the file object (`status: "AVAILABLE"`).

Errors:

| Code | `error`              | When                                                       |
| ---- | -------------------- | ---------------------------------------------------------- |
| 403  | `FORBIDDEN`          | caller is not the uploader                                 |
| 409  | `INCOMPLETE_UPLOAD`  | chunks missing: `{"error":"INCOMPLETE_UPLOAD","missing_chunks":[0,2]}` (at most the first 100) |
| 409  | `SIZE_MISMATCH`      | all chunks present but the total differs: `{"error":"SIZE_MISMATCH","expected_bytes":10,"received_bytes":6}` |
| 409  | `FILE_NOT_UPLOADING` | the file is in another state (e.g. `FAILED`)               |

---

## `GET /files/{fileId}/chunks/{n}` download one chunk

Any participant of the conversation, once the file is `AVAILABLE`.

Response: `200 OK`, `Content-Type: application/octet-stream`, body = the
ciphertext bytes of chunk `n`.

Errors:

| Code | `error`               | When                                       |
| ---- | --------------------- | ------------------------------------------ |
| 400  | `INVALID_CHUNK_INDEX` | malformed `n`, or `n` ≥ `chunk_count`      |
| 409  | `FILE_NOT_AVAILABLE`  | the upload is not complete yet             |
| 500  | `STORAGE_ERROR`       | the chunk is missing from the server's disk |

---

## `DELETE /files/{fileId}` delete a file

The uploader, or an `admin` of the conversation. The chunks are removed from
disk immediately; afterwards the file answers `404 FILE_NOT_FOUND`.

Response: `204 No Content`

Errors:

| Code | `error`     | When                                           |
| ---- | ----------- | ---------------------------------------------- |
| 403  | `FORBIDDEN` | caller is neither the uploader nor an admin    |

Other participants are not notified yet: their client sees the 404 the next
time it tries to download (WebSocket notifications are not implemented).

---

## Client flow

### Upload

1. Encrypt the file in chunks (see [Encryption](#encryption)).
2. `POST /files` with the conversation, total ciphertext size and chunk count.
3. `PUT` each chunk. On a failure, retry that chunk.
4. `POST /files/{id}/complete`.
5. Send the [file message](#file-message) in the conversation (Messages API).
   Only send it after `complete` succeeds, so recipients never see a file
   they cannot download yet.

To **resume** after a crash or a dropped connection: `GET /files/{id}`, then
send only the chunks that are not in `received_chunks`, then `complete`.

The Qt client (`client/src/network/FileService.cpp`) retries each request up
to 3 times on network errors and 5xx answers, and deletes the file on the
server if the upload fails. It does not resume after the client restarts: the
encryption state of a stream is not saved, so the upload starts over.

### Download

1. Decrypt the file message to get `file_id`, `key` and `header`.
2. `GET /files/{id}` to check `status` and `chunk_count`.
3. `GET` chunks `0 … chunk_count-1` in order, decrypting each one as it
   arrives and writing the plaintext to disk.
4. If a chunk fails to decrypt, or the last chunk does not carry the final
   tag, **delete the partial file**: it was tampered with or truncated. (The
   Qt client writes to a temporary file that only replaces the destination
   once the whole file has been checked.)

### Encryption

Use libsodium's `crypto_secretstream_xchacha20poly1305`:

- Generate a new random key per file (`crypto_secretstream_xchacha20poly1305_keygen`).
- `init_push` gives a 24-byte `header`; it is not secret and travels in the
  file message with the key.
- Split the plaintext into pieces of at most **1 MiB** (1 048 576 bytes);
  `push` each one, with tag `TAG_FINAL` on the last one and `TAG_MESSAGE`
  otherwise. Each pushed piece is one chunk (+17 bytes).
- So `chunk_count = ceil(plaintext_size / 1 MiB)` (at least 1) and
  `size_bytes = plaintext_size + 17 × chunk_count`.
- Each chunk is authenticated, and the stream detects chunks that are
  missing, reordered or replaced, and truncation (no final tag).

### File message

The key goes to the other participants inside a normal end-to-end encrypted
message (`POST /messaging/conversations/{id}/messages`), encrypted with the
conversation key like any text message. Its plaintext is JSON:

```json
{
  "kind": "file",
  "file_id": "3bae935a-a384-4e10-bfd6-21295d82cac3",
  "key": "<base64, 32 bytes>",
  "header": "<base64, 24 bytes>",
  "name": "scan.pdf",
  "size": 1548559,
  "mime": "application/pdf",
  "text": "Here's the scan from this morning"
}
```

- It is sent with **`encryption_version: 3`**. Text messages use `2`, so a
  text that happens to look like this JSON is still shown as text.
- `size` is the **plaintext** size, for display.
- `text` is an optional caption shown with the file. `mime` may be empty.
- `name` comes from the sender: the client keeps only its last path
  component (no `../`, no folders) before offering it in the save dialog.
- The client checks that `size` matches the server's `chunk_count` and
  `size_bytes` before downloading.

---

## Audit events

Sent by the file service to the audit service (`service_name: "file"`).
Metadata holds ids and sizes only, never names or keys.

| `event_type`            | `outcome` | When                                               |
| ----------------------- | --------- | -------------------------------------------------- |
| `file_upload_started`   | success   | `POST /files`                                      |
| `file_uploaded`         | success   | first successful `complete`                        |
| `file_download_started` | success   | chunk 0 downloaded (one event per download)        |
| `file_deleted`          | success   | file deleted (`deleted_by`: `uploader` or `admin`) |
| `file_access_denied`    | failure   | any 403 (`action` and `reason` in metadata)        |

## Not implemented yet

- Cleanup of uploads that are never completed (they stay `UPLOADING`).
- Notifying participants when a file is deleted.
- Compression and deduplication: compression must happen on the client
  before encryption; block-level deduplication conflicts with per-file
  random keys (identical files give different ciphertext).
