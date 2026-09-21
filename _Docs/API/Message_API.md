# Messages API (protected)

### `POST /conversations` create conversation

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Body:

```jsx
{
  "type": "direct",
  "participant_ids": ["uuid-2"],
  "name": null
}
```

`type` ∈ `direct | group`. `name` required for `group`, ignored for `direct`.

Response: 201 created

```jsx
{
  "conversation_id": "a12b...",
  "type": "direct",
  "name": null,
  "participant_ids": ["uuid-1", "uuid-2"],
  "created_at": "2026-09-02T14:00:00Z",
  "creator_id": "uuid-1"
}
```

Errors:

| 400 | Missing/invalid `participant_ids` or `type`                                                |
| --- | ------------------------------------------------------------------------------------------ |
| 401 | Unauthorized                                                                               |
| 403 | cannot create conversation between users. One or more participants have blocked the caller |
| 404 | A `participant_id` doesn't exist                                                           |
| 409 | A direct conversation between these two users already exists                               |
| 422 | `type: "group"` without a `name` (required data missing)                                   |
| 500 | internal failure                                                                           |

### `GET /conversations`  get all conversation for a specific user

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...", // server get userId from token
}
```

Response: 200 OK

```json
{
  "conversations": [
    {
      "conversation_id": "a12b...",
      "type": "group",
      "name": "Mission Team Alpha",
      "participant_ids": ["uuid-1", "uuid-2", "uuid-3"],
      "last_message_at": "2026-09-02T14:32:01Z",
      "unread_count": 3
    }
  ],
}
```

Errors: (is there any other errors here???)

| 401 | Unauthorized     |
| --- | ---------------- |
| 500 | internal failure |

### `GET /conversations/{conversation-id}`  retrieve ONE conversation

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response: 200 OK

```json
{
  "conversation_id": "a12b...",
  "type": "group",
  "name": "Mission Team Alpha",
  "participants": [
    { "user_id": "uuid-1", "role": "admin", "joined_at": "2026-08-01T09:00:00Z" },
    { "user_id": "uuid-2", "role": "member", "joined_at": "2026-08-01T09:05:00Z" }
  ],
  "created_at": "2026-08-01T09:00:00Z"
}
```

Errors:

| 401 | Unauthorized                   |
| --- | ------------------------------ |
| 403 | Caller not in the conversation |
| 404 | Conversation not found         |
| 500 | internal failure               |

### `DELETE /conversations/{id}` (chat can be deleted by admin only)

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response: 204 No Content

Errors:

| 400 | Bad request           |
| --- | --------------------- |
| 401 | Unauthorized          |
| 403 | Forbidden             |
| 404 | Not found             |
| 500 | Internal server error |
| 503 | Service unavailable   |

Notification body: (websocket notification to all chat members?)

```jsx
{
 "conversation_id": "a12b...",
 "status": "deleted"
}
```

### `DELETE /conversations/{id}/participants/me`  (user leave chat)

Headers:

```jsx
{
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response:

```json
{
  "conversation_id": "a12b...",
  "status": "LEFT"
}
```

`status` ∈ `left | deleted` (deleted only if caller is the last participant or an admin, per your permission rules).

Notification body : (Websocket push notification to all chat members

```jsx
{
 "conversation_id": "a12b...",
 "user-id": "uuid-1",
 "status": "LEFT"
}
```

### `DELETE /conversations/{id}/participants/{userId}`  (Admin takes of a conversation’s member)

Headers:

```jsx
{
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response:

```json
{
  "conversation_id": "a12b...",
  "status": "LEFT"
}
```

### `GET /conversations/{id}/history?before=&limit=`

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response: 200 OK

```json
{
  "messages": [
    {
      "message_id": "9f1c2e3a-...",
      "userId": "uuid",
      "ciphertext": "base64...",
      "nonce": "base64...",
      "server_timestamp": "2026-09-02T14:32:01Z",
      "attachments": []
    }
  ],
  "next_cursor": "opaque-cursor-string"
}
```

Errors:

| 400 | Invalid `before` cursor or `limit` |
| --- | ---------------------------------- |
| 401 | Unauthorized                       |
| 403 | Caller not a participant           |
| 404 | Conversation not found             |
| 500 | Internal failure                   |

### `POST /conversations/{id}/messages` (fallback send, no WebSocket)

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Request: same body as `message.send`, minus the `"type"` field.

```json
{
  "message_id": "9f1c2e3a-...",
  "ciphertext": "base64...",
  "nonce": "base64...",
  "encryption_version": 1,
  "client_timestamp": "2026-09-02T14:32:00Z",
  "attachments": [
    { "file_id": "uuid", "size_bytes": 1048576, "checksum_sha256": "hex" }
  ]
}
```

Response: 202 accepted/queued

```json
{
  "message_id": "9f1c2e3a-...",
  "status": "QUEUED",
  "server_timestamp": "2026-09-02T14:32:01Z"
}
```

`status` ∈ `queued | sent`.

| 202 | Accepted/queued                   |
| --- | --------------------------------- |
| 400 | Malformed body                    |
| 401 | Unauthorized                      |
| 403 | Caller not a participant          |
| 404 | Conversation not found            |
| 409 | Duplicate `message_id`            |
| 413 | Payload too large                 |
| 429 | Rate limit exceeded               |
| 500 | Internal failure                  |
| 503 | Downstream dependency unavailable |

### `PUT /conversations/{id}` update group, NICE TO HAVE

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Body (all fields optional — send only what changes):

```json
{
  "name": "Mission Team Alpha (Renamed)",
  "add_participant_ids": ["uuid-4"],
  "remove_participant_ids": ["uuid-3"]
}
```

Response: 200 OK

```json
{
  "conversation_id": "a12b...",
  "type": "group",
  "name": "Mission Team Alpha",
  "participants": [
    { "user_id": "uuid-1", "role": "admin", "joined_at": "2026-08-01T09:00:00Z" },
    { "user_id": "uuid-2", "role": "member", "joined_at": "2026-08-01T09:05:00Z" }
  ],
  "created_at": "2026-08-01T09:00:00Z"
}
```

Errors:

| 400 | malformed body                                        |
| --- | ----------------------------------------------------- |
| 401 | Unauthorized                                          |
| 403 | caller lacks the rights for this change               |
| 404 | A `participant_id` or `conversation_id` doesn't exist |
| 409 | conflict (ex: change role of last admin)              |
| 500 | internal failure                                      |

### NICE TO HAVE : delete message and edit message ?
