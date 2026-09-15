# LPTF — Development Certificates

## ⚠️ Warning

These certificates are **self-signed and committed intentionally** for development
convenience. They are not secret in this repository.

**NEVER use these in a production deployment.**  
For any real deployment, generate fresh certificates and never commit private keys.

---

## What is in this folder

| File | Used by | Purpose |
|---|---|---|
| `ca/ca.crt` | every service | The CA all services trust |
| `ca/ca.key` | nobody at runtime | Only needed to sign new certs |
| `<service>/<service>.crt` | that service only | Proves this service's identity to its peers |
| `<service>/<service>.key` | that service only | Private key, never shared |
| `ca/ca.srl` | nobody | OpenSSL's serial tracker, don't delete |

Unlike a client/server setup where only one side needs a certificate, mTLS
means every service must both prove its own identity and verify its peers —
so each of the 5 services has its own key+cert pair, all signed by the same
dev CA, and each cert's SAN (Subject Alternative Name) matches its Docker
Compose service name so hostname verification succeeds over the internal network.

---

## How to generate

Run these commands from the `certs/` directory.

### All platform
#### On windows
OpenSSL must be installed first. If it is not:
```powershell
winget install ShiningLight.OpenSSL
```

```bash
# Step 1 — Create the Certificate Authority (once)
openssl genrsa -out ca/ca.key 4096
openssl req -new -x509 -days 3650 -key ca/ca.key -out ca/ca.crt -subj "/CN=SecureChat-Dev-CA"

# Step 2 — For EACH service, repeat this pattern
# (shown once for auth; same 3 commands for messaging, file, audit, gateway)

openssl genrsa -out auth/auth.key 2048

openssl req -new -key auth/auth.key -out auth/auth.csr -subj "/CN=auth"
printf "subjectAltName=DNS:auth" > auth/auth.ext


# The SAN extension is the part Inferno's server-only setup didn't need —
# every service here needs to be verifiable by every other service,
# so each cert must declare its own DNS name explicitly.
openssl x509 -req -days 3650 -in auth/auth.csr -CA ca/ca.crt -CAkey ca/ca.key -CAcreateserial -out auth/auth.crt -extfile auth/auth.ext
```

### Linux / macOS
```bash
# Step 3 — Clean up CSRs (Linux/macOS)
rm */*.csr
```

### Windows (PowerShell)
```powershell
# Step 3 — Clean up CSRs (Windows PowerShell)
Remove-Item */*.csr
```

---

## Why self-signed

A real CA (like Let's Encrypt) would require a public domain name and renewal.
For a local development and testing environment, a self-signed CA is standard
practice — it provides real TLS encryption and server authentication,
just without a third-party trust chain.

