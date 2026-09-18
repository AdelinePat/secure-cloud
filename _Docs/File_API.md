# File API

Les différentes méthodes/Endpoint /Rôle

| Méthode  | Endpoint                  | Rôle                      |
| -------- | ------------------------- | ------------------------- |
| `POST`   | `/files`                  | Enregistrer un fichier    |
| `GET`    | `/files/{fileId}`         | Récupérer les métadonnées |
| `GET`    | `/files/{fileId}/content` | Télécharger le fichier    |
| `DELETE` | `/files/{fileId}`         | Supprimer un fichier      |

## `POST /files`  enregistrer un fichier

Headers:

```jsx
{
 "Content-Type":  "multipart/form-data"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Body:

```jsx
{
 "file": "ByteArray",
 "userId": "user123"
}
```

Response: 201 Created

```json
{
 "fileId": "f_8f7d2a91",
 "userId": "user_123",
 "size": 2458173,
 "contentType": "application/octet-stream",
 "checksum": "sha256-of-ciphertext",
 "status": "AVAILABLE",
 "createdAt": "2026-09-02T12:42:00Z"
}
```

Errors:

| 400 | requête invalide        |
| --- | ----------------------- |
| 413 | fichier trop volumineux |
| 415 | type non supporté       |
| 500 | erreur interne          |
| 503 | stockage indisponible   |

---

## `GET /files/{id}` Récupérer les métadonnées d’un fichier

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response: 200 OK

JSON:

```json
{
 "fileId": "f_8f7d2a91",
 "userId": "user_123",
 "size": 2458173,
 "contentType": "application/octet-stream",
 "checksum": "sha256-of-ciphertext",
 "status": "AVAILABLE",
 "createdAt": "2026-09-02T12:42:00Z"
}
```

Errors:

| 401 | appel non authentifié                      |
| --- | ------------------------------------------ |
| 403 | appelant non autorisé à accéder au fichier |
| 404 | fichier inexistant                         |

---

## `GET /files/{id}/content` Télécharger le fichier

Headers:

```jsx
{
 "Content-Type":  "multipart/form-data"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response:  200 OK

Headers:

```jsx
{
 "Content-Type":  "application/octet-stream"
}
```

Body? ⇒ <binary data>

Errors:

| 401 | appel non authentifié                      |
| --- | ------------------------------------------ |
| 403 | appelant non autorisé à accéder au fichier |
| 404 | fichier inexistant                         |

---

## `DELETE /files/{id}`  Supprimer un fichier

Headers:

```jsx
{
 "Content-Type": "application/json"
 "Authorization": "Bearer eyJhbGciOi...",
}
```

Response: 204 No Content ( Suppression immédiate )

puis notification push websocket aux utilisateurs concernés (du chat)

Notification body:

```jsx
{ 
 "fileId": "f_8f7d2a91",
 "status": "DELETED",
 "conversationId": "convId", // optional??
 "userId": "user_123", // meta data to know who deleted it?
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
