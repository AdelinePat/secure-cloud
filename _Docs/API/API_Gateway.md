# API Gateway (public)

## Users/Auth

`POST /secureChat/auth/login` >> Auth API POST /auth/login

`POST /secureChat/users`  >> Auth API POST /users

`PATCH /secureChat/users/{id}` (protected) >> Auth API PATCH /users/{id}

`DELETE /secureChat/users/{id}` (protected) >> Auth API DELETE /users/{id}

The gateway forwards the `Authorization: Bearer <jwt>` header unchanged.
The messaging service verifies the token itself with the auth public key, so
the gateway never injects a caller identity header.

## Conversations

`POST /secureChat/conversations` (protected) >> Messaging API POST /conversations

`GET /secureChat/conversations` (protected) >> Messaging API GET /conversations

`GET /secureChat/conversations/{id}` (protected) >> Messaging API GET /conversations/{id}

`DELETE /secureChat/conversations/{id}` (protected) >> Messaging API DELETE /conversations/{id}

`DELETE /secureChat/conversations/{id}/participants/me` (protected) >> Messaging API DELETE /conversations/{id}/participants/me

`DELETE /secureChat/conversations/{id}/participants/{userId}` (protected) >> Messaging API DELETE /conversations/{id}/participants/{userId}

### Nice to have

`PUT /secureChat/conversations/{id}` (protected) >> Messaging API PUT /conversations/{id}

## Messages

`GET /secureChat/conversations/{id}/history?before=&limit=` (protected) >> Messaging API GET /conversations/{id}/history?before=&limit=

`POST /secureChat/conversations/{id}/messages` (protected) >> Messaging API POST /conversations/{id}/messages

Messages are end-to-end encrypted by the client: the request carries base64
`ciphertext`/`nonce`, and the messaging service only stores and returns those
opaque values. It never sees plaintext.

### Nice to have (what route to use???)

`DELETE /secureChat/messages/{id}` (protected) >> to write

`PUT/PATCH /secureChat/messages/{id}` (protected) >> to write

## Files

`POST /secureChat/files` (protected) >> Files API POST /files

`GET /secureChat/files/{id}` (protected) >> File API GET /files/{id}

`GET /secureChat/files/{id}/content` (protected) >> File API GET /files/{id}/content

`DELETE /secureChat/files/{id}` (protected) >> File API DELETE /files/{id}

## Inter MS, not in api gateway?

`GET /secureChat/users/{id}` (protected) >> Auth API GET /users/{id}
