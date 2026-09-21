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

`POST /secureChat/files` (protected) >> Files API POST /files

`GET /secureChat/files/{id}` (protected) >> File API GET /files/{id}

`GET /secureChat/files/{id}/content` (protected) >> File API GET /files/{id}/content

`DELETE /secureChat/files/{id}` (protected) >> File API DELETE /files/{id}

## Inter MS, not in api gateway?

`GET /secureChat/users/{id}` (protected) >> Auth API GET /users/{id}
