# Audit API (MS to MS only)

Internal service used by the other microservices to record security-relevant
actions. It is **not** routed through the API gateway: only services on the
internal network can reach it.

Base URL inside Docker Compose: `https://audit:8085`

## Transport & authentication

- HTTPS with **mutual TLS**. Every service presents its certificate from
  `certs/<service>/<service>.crt`, signed by `certs/ca/ca.crt`.
- The caller identity is the **CN of the client certificate** (`auth`, `file`,
  `messaging`, `gateway`).
- On append, `service_name` must equal the certificate CN. `file` cannot emit
  events attributed to `auth`.
- Only services listed in `AUDIT_ALLOWED_SERVICES` may call the API.
- Requests may carry `X-Request-Id`; it is echoed in the response and included
  in logs. A UUID is generated when absent.

TLS can be disabled for local development with `AUDIT_TLS_ENABLED=false`; caller
identity checks are then disabled (`AUDIT_REQUIRE_CALLER_IDENTITY` defaults to
the TLS setting). Never run production this way.

## Storage model

Append-only collection `auditLogs` in MongoDB. Each event carries `seq`,
`prev_hash` and `hash`:

```
hash = sha256(v1 | id | seq | event_type | service_name | actor_id | target_id |
              outcome | occurred_at | recorded_at | canonical(metadata) | prev_hash)
```

`prev_hash` of `seq: 1` is 64 zeros. Because every event commits to its
predecessor, editing or deleting a stored event breaks the chain and is
reported by `GET /audit/verify`.

Writes are serialised with unique indexes on `seq`, `prev_hash` and `hash`
instead of transactions, so a standalone `mongod` is enough. Concurrent writers
retry against the new head.

## Event object

```json
{
  "id": "0f4d9f0e-3d6a-4a0e-9a0e-2b7a3c9d1e2f",
  "seq": 42,
  "event_type": "file_uploaded",
  "service_name": "file",
  "actor_id": "8f3c1b7e-...",
  "target_id": "f_8f7d2a91",
  "outcome": "success",
  "occurred_at": "2026-10-21T09:30:12.345Z",
  "recorded_at": "2026-10-21T09:30:12.401Z",
  "metadata": { "size_bytes": 2458173, "ip": "10.0.0.4" },
  "prev_hash": "9a1f...",
  "hash": "c7e2..."
}
```

| Field | Rules |
| --- | --- |
| `event_type` | required, 1–64 chars, `[a-z0-9_.:-]`, e.g. `login_failed`, `file_uploaded`, `conversation_created` |
| `service_name` | required, 1–64 chars, alphanumeric/`.`/`_`/`-`; must match cert CN |
| `outcome` | required, `success` or `failure` |
| `actor_id` | optional user id, 1–128 printable chars |
| `target_id` | optional resource id (file, conversation, message…), 1–128 printable chars |
| `occurred_at` | optional ISO8601; defaults to now when omitted |
| `metadata` | optional JSON **object**, max 16 KiB after canonicalisation (keys sorted) |
| `id` / `event_id` | optional caller-supplied idempotency key, 1–64 chars `[A-Za-z0-9_-]` |
| `seq`, `recorded_at`, `prev_hash`, `hash` | server-owned, ignored on input |

---

## `POST /audit/events` — append one event

Request body: event object (`event_type`, `service_name`, `outcome` required).

Response `201 Created`: the sealed event (as above).

Re-sending the same `event_id` returns `200 OK` with the previously stored
event and does **not** append a duplicate. This is the retry contract for
"never lose an audit record".

| Status | Meaning |
| --- | --- |
| 201 | event appended |
| 200 | idempotent replay, nothing written |
| 400 | invalid JSON / validation error |
| 403 | certificate CN missing, not allowed, or ≠ `service_name` |
| 409 | chain contention exhausted its retries |
| 500 | unexpected error |
| 503 | MongoDB unavailable |

Example:

```bash
curl --cert certs/file/file.crt --key certs/file/file.key \
     --cacert certs/ca/ca.crt \
     -X POST https://audit:8085/audit/events \
     -H 'Content-Type: application/json' \
     -d '{
           "event_id": "5d2f3b2e-8a1c-4a99-a1b1-3f0e6a7c9b10",
           "event_type": "file_uploaded",
           "service_name": "file",
           "actor_id": "8f3c1b7e-...",
           "target_id": "f_8f7d2a91",
           "outcome": "success",
           "metadata": { "size_bytes": 2458173 }
         }'
```

## `POST /audit/events/batch` — append several events

```json
{ "events": [ { ... }, { ... } ] }
```

Appended in order; 1–500 entries. Response `201`:
`{ "appended": 2, "events": [ ... ] }`.

If an entry fails, processing stops and the response is the corresponding
error plus `"appended": n` for the entries already stored. Give each event an
`event_id` if the emitter may retry a partially accepted batch.

## `GET /audit/events` — query

| Query param | Meaning |
| --- | --- |
| `event_type` | exact match |
| `service_name` | exact match |
| `actor_id`, `target_id` | exact match |
| `outcome` | `success` / `failure` |
| `from`, `to` | inclusive ISO8601 bounds on `occurred_at` |
| `before_seq` | keyset pagination: `seq < before_seq` |
| `after_seq` | keyset pagination: `seq > after_seq` |
| `limit` | default 50, max 500 |
| `order` | `desc` (default) or `asc` |

Response `200`:

```json
{ "events": [ ... ], "count": 2 }
```

`before_seq` and `after_seq` are mutually exclusive.

## `GET /audit/events/{id}` — fetch one event

`200` with the event, `404 NOT_FOUND` when it does not exist.

## `GET /audit/verify` — verify the chain

Walks the entire collection in `seq` order and recomputes every hash.

```json
{ "valid": true, "checked": 1204, "first_broken_seq": null, "reason": "" }
```

On tampering:

```json
{
  "valid": false,
  "checked": 41,
  "first_broken_seq": 42,
  "reason": "stored hash does not match the event content"
}
```

## `GET /health`

Unauthenticated (still behind the TLS handshake):

```json
{ "status": "ok", "mongo": "up" }
```

`503` with `"status":"degraded"` when MongoDB is unreachable.

---

## Errors

Same shape as the public API:

```json
{
  "code": 400,
  "type": "VALIDATION_ERROR",
  "message": "outcome must be 'success' or 'failure'",
  "request_id": "3f0e6a7c-..."
}
```

Types: `VALIDATION_ERROR`, `FORBIDDEN`, `NOT_FOUND`, `CONFLICT`,
`STORAGE_UNAVAILABLE`, `INTERNAL_ERROR`.

## Open items

- **Encryption at rest**: the spec requires encrypted audit logs. Options are
  MongoDB encrypted storage (WiredTiger + KMIP) or application-level envelope
  encryption of `metadata`/free-text fields with a key from the auth service.
  Not implemented yet.
- **Signed checkpoints**: periodically sign `(last_seq, last_hash)` so the chain
  can be verified without walking every event (`audit_checkpoints` in the
  original DBML). Without a checkpoint, truncating the tail of the chain is not
  detectable: `GET /audit/verify` only sees the events that are still stored.
- **Rate limiting / backpressure** per emitting service.
- **Read access**: every service in `AUDIT_ALLOWED_SERVICES` can currently read
  the whole log. Tighten this to `gateway` (for an admin UI) once roles exist,
  and keep per-service read scoping separate from the write allow-list.
- **Retention**: append-only conflicts with deleting old events; decide on an
  archival policy (export + signed checkpoint) before adding any TTL index.
