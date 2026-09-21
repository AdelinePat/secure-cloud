# Auth API

## USERS API (part of Auth API)

### `POST /users`  Création USER

Body:

```jsx
{
 "username": "abc",
 "password": "clear password",
 "public_key": "...",
  "key_algorithm": "X25519"
}
```

Response : 201 created

Errors:

| 400 Bad Request           |                                                  |
| ------------------------- | ------------------------------------------------ |
| 409 Conflict              | (si jamais on décide que le username est unique) |
| 422 Unprocessable Content | ( mot de passe trop court, username invalide?)   |
| 429 Too Many Requests     |                                                  |
| 500 Internal Server Error |                                                  |

### `PATCH /users/{id}`  modification d’un user (PUT ou PATCH?) protected

Headers :

```jsx
{
 "Authorization": "Bearer eyJhbGciOi...",
 "Content-Type": "application/json"
}
```

Body:

```jsx
{
 "username": "abc",
}
OU
{
 "password": "clear password",
}
```

| 204 No Content            | user updated, no response body                     |
| ------------------------- | -------------------------------------------------- |
| 400 Bad Request           | malformed JSON / invalid request format            |
| 401 Unauthorized          | not authenticated                                  |
| 403 Forbidden             | authenticated but not allowed to modify this user  |
| 404 Not Found             | user doesn't exist                                 |
| 409 Conflict              | username is already taken                          |
| 422 Unprocessable Content | username/password doesn't satisfy validation rules |
| 429 Too Many Requests     | rate limit exceeded                                |
| 500 Internal Server Error | unexpected server error                            |

### `GET /users/{id}`  protected

Headers:

```jsx
{
 "Authorization": "Bearer eyJhbGciOi...",
 "Content-Type": "application/json"
}
```

Response: 200 OK

```jsx
{
  "id": "123",
  "username": "abc"
}
```

| **400 Bad Request**           | The `{id}` parameter is malformed/invalid             |
| ----------------------------- | ----------------------------------------------------- |
| **401 Unauthorized**          | Missing, invalid, or expired authentication           |
| **403 Forbidden**             | Authenticated, but not authorized to access this user |
| **404 Not Found**             | No user with that ID exists                           |
| **429 Too Many Requests**     | Rate limit exceeded                                   |
| **500 Internal Server Error** | Unexpected server-side failure                        |

### `DELETE /users/{id}`  protected

Headers :

```jsx
{
 "Authorization": "Bearer eyJhbGciOi...",
 "Content-Type": "application/json"
}
```

Response:

| 204 No Content     | successfully deleted                           |
| ------------------ | ---------------------------------------------- |
| 401 Unauthorized   | not authenticated                              |
| 403 Forbidden      | not allowed to delete this user                |
| 404 Not Found      | user doesn't exist                             |
| 409 Conflict       | cannot delete because of a conflict/dependency |
| 500 Internal Error | unexpected server error                        |

## Auth API

To log in :

### `POST /auth/login`

```jsx
{
 "username": "user",
 "password": "clear password"
}
```

check credentials from here

Response: 200 OK

### `GET /auth/.well-known/jwks.json` get public key to check token signature

`.well-known/jwks.json` prefix is itself a standard (RFC 8615) — it's where clients are expected to look for this kind of discovery metadata without you needing to advertise the path separately.

Response:

```jsx
{
  "keys": [
    {
      "kty": "EC",
      "crv": "P-256",
      "x": "base64url-encoded-x-coordinate",
      "y": "base64url-encoded-y-coordinate",
      "kid": "auth-key-2026-09",
      "use": "sig",
      "alg": "ES256"
    }
  ]
}
```

Field meanings:

- `kty` — key type ("EC" for elliptic curve, would be "RSA" if you'd picked RSA)
- `crv` — which curve (P-256 for ES256)
- `x`, `y` — the actual public key coordinates, this is the math your services will feed into their "verify signature" call
- `kid` — key ID, a label so a token can say "I was signed with key X" — matters once you eventually rotate keys and have more than one valid public key at a time; for MVP with one permanent key it's not load-bearing, but cheap to include now and free to ignore later
- `use` — "sig" meaning "this key is for verifying signatures" (as opposed to encryption)
- `alg` — which algorithm this key is meant to be used with
