# Development certificates

Nothing in this folder is committed except the folder placeholders. The
certificates are generated on each machine by the one-shot `certs` Compose
service (`tools/ensure-certs.sh`) the first time you run `docker compose up`.

**These are development certificates.** For a real deployment, use
certificates from your own PKI and keep the CA key off the servers.

---

## What gets generated

| File | Mounted into | Purpose |
|---|---|---|
| `private/ca.key` | nothing (only the `certs` service) | Signs the service certificates |
| `ca/ca.crt` | every service, and the desktop client | The CA everyone trusts |
| `<service>/<service>.key` | that service only | Its private key |
| `<service>/<service>.crt` | that service only | `CN=<service>`, SAN `DNS:<service>` |

All keys are ECDSA P-256. Service certificates are valid for 825 days, the CA
for 10 years.

Every service has both a key and a certificate because the audit service uses
mTLS: callers prove who they are with their certificate (audit reads the CN),
and they check audit's certificate in return. Each SAN matches the Compose
service name so host name verification works on the internal network.

The gateway's certificate also covers `localhost` and `127.0.0.1`, because
the desktop client connects to it through the published port over HTTPS.

## When certificates are replaced

`ensure-certs.sh` runs on every `docker compose up` and keeps what is still
good. It replaces:

- the CA, when it is missing or expires within 30 days. All service
  certificates are then reissued, since they no longer verify;
- a service certificate, when it is missing, not signed by the current CA,
  expires within 30 days, or does not have the expected SANs.

To start over with a new CA, delete the generated files and run
`docker compose up` again:

```bash
find certs -type f ! -name .gitkeep ! -name README.md -delete
```

If you regenerate the CA, restart the desktop client so it loads the new
`ca/ca.crt`.

## Connecting from another machine

To reach the gateway through a LAN IP or host name, add it to the gateway
certificate in `.env`, then run `docker compose up` again (only the gateway
certificate is reissued):

```bash
GATEWAY_EXTRA_SANS=IP:192.168.1.20,DNS:securechat.lan
```

On the other machine, copy `certs/ca/ca.crt` and start the client with:

```bash
SECURECHAT_GATEWAY_URL=https://192.168.1.20:8080 \
SECURECHAT_CA_FILE=/path/to/ca.crt ./client
```

## Inspecting a certificate

```bash
openssl x509 -in certs/gateway/gateway.crt -noout -subject -ext subjectAltName -enddate
openssl verify -CAfile certs/ca/ca.crt certs/gateway/gateway.crt
curl --cacert certs/ca/ca.crt https://localhost:8080/health
```
