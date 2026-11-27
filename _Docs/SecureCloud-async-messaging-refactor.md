# Async Message refactor

## Before

the service accepted one connection, handled it fully, then accepted the next. Everything shared one database connection.
All in messaging/src/main.cpp
                                                                                                  
## Now

- Server (messaging/src/http/HttpServer.cpp): reading requests and writing responses no longer blocks, so a slow client can't hold anyone up. Handlers run on a pool of worker threads, 8 by default (set with MESSAGING_WORKERS), so a slow query only ties up one worker.
- Connection pool (messaging/src/db/ConnectionPool.cpp): each request borrows its own Postgres connection, and a connection that dropped is reopened the next time it's borrowed.
- Router (messaging/src/http/MessagingRouter.cpp): the endpoint logic moved out of main.cpp, with one method per endpoint. Status codes and error strings are unchanged, so the gateway and client need no changes.
- IUserDirectory: the two raw SQL queries on auth's users table now sit behind this interface.
- main.cpp: down from 641 lines to about 130, and it shuts down cleanly on SIGTERM.

One small behavior change: a query string like ?limit=5 no longer causes a 404. It's ignored, as before for history.

## Tests

- Unit tests: 38 pass in the project's Docker image. 19 are new router tests covering every endpoint, auth failures, membership checks and audit events. 4 are new server tests, including one where a blocked request doesn't stop a second request from being answered. The existing Postgres-only test still skips without a test database.
- End to end: I started the full stack with docker compose up and went through the gateway as alice and bob. Health, create, list, send, a duplicate message (409), history, peer key and a missing token (401) all gave the expected results. 100 parallel sends, 20 at a time, all returned 201. I didn't time it against the old version. I stopped the stack afterwards with docker compose down (volumes kept).

## Note for group E2EE work: 

the peer-key logic is now in MessagingRouter::getPeerKey, and key storage is behind IUserDirectory. If they start after you merge, that's where to look.

Not done: history paging still returns the 50 most recent messages and ignores before and limit. That's a small change in getHistory now.