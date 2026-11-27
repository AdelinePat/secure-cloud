#!/bin/sh
# Bootstraps the development PKI: one CA, and one key + certificate per
# service, used for mTLS between services and for the gateway's HTTPS.
#
# Run by the one-shot `certs` Compose service before every other service.
# Nothing here is committed: each machine gets its own CA on first start.
#
#   <certs_dir>/private/ca.key          CA private key (mounted by nothing else)
#   <certs_dir>/ca/ca.crt               CA certificate (mounted everywhere,
#                                       and trusted by the desktop client)
#   <certs_dir>/<service>/<service>.key service private key
#   <certs_dir>/<service>/<service>.crt service certificate, CN=<service>
#
# Existing files are kept. The script replaces:
#   - the CA when it is missing or expires within 30 days (which then
#     replaces every service certificate, since they no longer verify),
#   - a service certificate when it is missing, not signed by the current CA,
#     expires within 30 days, or does not carry the expected SANs.
#
# GATEWAY_EXTRA_SANS adds names to the gateway certificate, for a demo where
# clients connect through another host name or IP, e.g.
#   GATEWAY_EXTRA_SANS="IP:192.168.1.20,DNS:securechat.lan"
#
# Usage: ensure-certs.sh <certs_dir>
set -eu

CERTS_DIR="${1:?usage: ensure-certs.sh <certs_dir>}"
SERVICES="auth messaging file audit gateway"
CA_DAYS=3650
LEAF_DAYS=825
RENEW_BEFORE=2592000  # 30 days, in seconds

umask 077
mkdir -p "$CERTS_DIR/private" "$CERTS_DIR/ca"
CA_KEY="$CERTS_DIR/private/ca.key"
CA_CRT="$CERTS_DIR/ca/ca.crt"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

log() { echo "[certs] $*"; }

new_key() {  # <out>: EC P-256 key, written atomically
    openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$1.tmp"
    mv "$1.tmp" "$1"
    chmod 600 "$1"
}

expected_sans() {  # <service>: comma-separated SAN list
    sans="DNS:$1"
    if [ "$1" = gateway ]; then
        # The desktop client reaches the gateway through the published port.
        sans="$sans,DNS:localhost,IP:127.0.0.1"
        if [ -n "${GATEWAY_EXTRA_SANS:-}" ]; then
            sans="$sans,$GATEWAY_EXTRA_SANS"
        fi
    fi
    echo "$sans"
}

# SANs of a certificate, normalised to the "DNS:a,IP:b" form used above.
actual_sans() {
    openssl x509 -in "$1" -noout -ext subjectAltName 2>/dev/null \
        | tail -n +2 | tr -d ' \n' | sed 's/IPAddress:/IP:/g'
}

# ── Old layout ───────────────────────────────────────────────────────────────
# The CA key used to live in ca/, which every service mounts. It was also
# committed to git, so it is not secret: drop it and start a new CA.
if [ -e "$CERTS_DIR/ca/ca.key" ]; then
    log "removing ca/ca.key from the old layout; generating a new CA"
    rm -f "$CERTS_DIR/ca/ca.key" "$CERTS_DIR/ca/ca.srl" "$CA_CRT"
fi
rm -f "$CERTS_DIR"/*/*.csr "$CERTS_DIR"/*/*.ext

# ── CA ───────────────────────────────────────────────────────────────────────
if [ -s "$CA_KEY" ] && [ -s "$CA_CRT" ] &&
   openssl x509 -in "$CA_CRT" -noout -checkend "$RENEW_BEFORE" >/dev/null 2>&1; then
    log "keeping existing CA"
else
    log "generating a new CA"
    new_key "$CA_KEY"
    openssl req -x509 -new -key "$CA_KEY" -sha256 -days "$CA_DAYS" \
        -subj "/CN=SecureChat Dev CA" \
        -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -out "$CA_CRT.tmp"
    mv "$CA_CRT.tmp" "$CA_CRT"
fi
chmod 644 "$CA_CRT"

# ── Service certificates ─────────────────────────────────────────────────────
for service in $SERVICES; do
    dir="$CERTS_DIR/$service"
    key="$dir/$service.key"
    crt="$dir/$service.crt"
    sans="$(expected_sans "$service")"
    mkdir -p "$dir"

    if [ -s "$key" ] && [ -s "$crt" ] &&
       openssl verify -CAfile "$CA_CRT" "$crt" >/dev/null 2>&1 &&
       openssl x509 -in "$crt" -noout -checkend "$RENEW_BEFORE" >/dev/null 2>&1 &&
       [ "$(actual_sans "$crt")" = "$sans" ]; then
        log "keeping $service certificate"
        continue
    fi

    log "issuing $service certificate ($sans)"
    new_key "$key"
    cat > "$WORK/$service.ext" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth,clientAuth
subjectAltName=$sans
EOF
    openssl req -new -key "$key" -subj "/CN=$service" -out "$WORK/$service.csr"
    openssl x509 -req -in "$WORK/$service.csr" -CA "$CA_CRT" -CAkey "$CA_KEY" \
        -set_serial "0x$(openssl rand -hex 16)" -sha256 -days "$LEAF_DAYS" \
        -extfile "$WORK/$service.ext" -out "$crt.tmp"
    mv "$crt.tmp" "$crt"
    chmod 644 "$crt"
done

# Files are created by root inside the container; give them to whoever owns
# the certs directory on the host, so they can be read and deleted there.
owner="$(stat -c '%u:%g' "$CERTS_DIR")"
chown -R "$owner" "$CERTS_DIR"

log "CA fingerprint: $(openssl x509 -in "$CA_CRT" -noout -fingerprint -sha256)"
