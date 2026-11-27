# Client-Gateway communication & private certification

The desktop client now talks HTTPS to the gateway, and private keys are no longer in git: they're generated on each machine at startup. Everything is staged on feat/tls-certs but not committed. 

## Key generation

- Generator: tools/ensure-certs.sh runs in a one-shot certs Compose service before everything else, 
  the same pattern as jwt-keys. It creates a dev CA and one EC P-256 key and cert per service. It
  keeps what's still valid and replaces the CA or a cert when it's missing, expiring within 30 days,
  signed by another CA, or has the wrong names.
- CA key isolation: the CA key now lives in certs/private/, which only the certs container mounts.  
  Before, it sat in certs/ca/, so any service with access to that folder could read it. Each service
  now gets only certs/ca/ (just ca.crt) plus its own folder, both read-only. The existing           /app/certs/... paths didn't change, so the audit mTLS config is untouched.
- Git: the committed keys and certs are removed from the repo and certs/*/* is gitignored. Empty .gitkeep files keep the folders, because Compose won't mount a folder that doesn't exist.

## Gateway TLS

- HTTPS on 8080: the gateway requires TLS 1.2 or later when GATEWAY_TLS_CERT and GATEWAY_TLS_KEY are 
  set, and refuses to start if the key doesn't match the cert. Without them it stays plain HTTP, which
  the security test harness and the existing tests rely on.                                          
- Timeouts: I added a 30 s timeout on the TLS handshake and 60 s on reading a request. Before, aconnection that opened and then sent nothing stayed open forever.

## Client

- It connects to https://localhost:8080 and trusts only the dev CA, not the system store. It finds certs/ca/ca.crt by walking up from its own folder, so it works from client/build/ and build/Release/.
- SECURECHAT_GATEWAY_URL and SECURECHAT_CA_FILE override the address and the CA, for a LAN demo for example.
- If the CA file is missing, the client shows an error telling you to run docker compose up first.

Tests
- Gateway: new TLS tests: a client that trusts the cert gets a response, one that doesn't is rejected, plain HTTP gets no answer, and a mismatched key or missing file is refused.
- Client: real TLS handshake against a local test server, both trusted and untrusted.
- Generator: run against a scratch folder, it creates everything on the first run, keeps everything on the second, and reissues only the gateway cert when the extra names change.
- Full stack from an empty certs/:
  - The whole smoke test passes over HTTPS.
  - Plain HTTP, a client without the CA, and TLS 1.1 are all refused.
  - Audit events from that run reached MongoDB, so mTLS works with the new certs.
  - I stopped the stack afterwards.

Before you commit:
- Pulling this branch deletes everyone's existing cert files. That's intended: the next docker compose up creates a new CA. The old CA key stays in git history, but nothing trusts that CA any more. Tell your colleague, and note that a running client must be restarted after a new CA is generated.
- The client test contains a hard-coded throwaway private key. It only signs a test cert and is used only by that test, like the existing auth/tests/fixtures.
- I haven't tried the client on Windows. The CA lookup works the same way there, but it's worth one run with windows-build.bat on your colleague's machine.
- Internal traffic is still plain HTTP. The gateway reaches auth, messaging and file over plain HTTP on the Docker network. That's fine for the MVP since nothing on that network is exposed, but the certs needed for mTLS there now exist.

README.md and certs/README.md are updated with how it works, when certs are renewed, and how to connect from another machine.