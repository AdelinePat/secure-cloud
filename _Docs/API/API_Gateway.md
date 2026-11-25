# API Gateway (public)

## Users/Auth

`POST /secureChat/auth/login` >> Auth API POST /auth/login

`POST /secureChat/users`  >> Auth API POST /users

`PATCH /secureChat/users/{id}` (protected) >> Auth API PATCH /users/{id}

`DELETE /secureChat/users/{id}` (protected) >> Auth API DELETE /users/{id}

## Conversations

`POST /secureChat/conversations` (protected) >> Message API POST /conversation

`GET /secureChat/conversations` (protected) >> Message API GET /conversations

`GET /secureChat/conversations/{id}` (protected) >> Message API GET /conversation/{id}

`DELETE /secureChat/conversations/{id}` (protected) >> Message API DELETE /conversations/{id}

`DELETE /secureChat/conversations/{id}/{userId}` (protected) >> Message API DELETE /conversations/{id}/{userId}

### Nice to have

`PUT /secureChat/conversations/{id}` (protected) >> Message API PUT /conversations/{id}

## Messages

`GET /secureChat/conversations/{id}/history?before=&limit=` (protected) >> Message API GET /conversations/{id}/history?before=&limit=

`POST /secureChat/conversations/{id}/messages` (protected) >> Message API POST /conversations/{id}/messages

### Nice to have (what route to use???)

`DELETE /secureChat/messages/{id}` (protected) >> to write

`PUT/PATCH /secureChat/messages/{id}` (protected) >> to write

## Files

Implemented: the gateway forwards `/files` and `/files/...` unchanged to the
file service (body limit 2 MiB on these routes). See [File API](./File_API.md).

`POST /files` (protected) >> File API POST /files

`PUT /files/{id}/chunks/{n}` (protected) >> File API PUT /files/{id}/chunks/{n}

`GET /files/{id}` (protected) >> File API GET /files/{id}

`POST /files/{id}/complete` (protected) >> File API POST /files/{id}/complete

`GET /files/{id}/chunks/{n}` (protected) >> File API GET /files/{id}/chunks/{n}

`DELETE /files/{id}` (protected) >> File API DELETE /files/{id}

## Inter MS, not in api gateway?

`GET /secureChat/users/{id}` (protected) >> Auth API GET /users/{id}
