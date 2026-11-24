#!/bin/sh
# Bootstraps the auth service's ES256 (P-256) JWT key pair.
#
# Run by the one-shot `jwt-keys` Compose service before auth and messaging:
#   - creates <key_dir>/private.pem if it does not exist (kept otherwise),
#   - always re-derives <key_dir>/public.pem from the private key,
#   - publishes the public key alone to <public_dir>/public.pem for the
#     services that only verify tokens (they never see the private key).
#
# Usage: ensure-jwt-keys.sh <key_dir> <public_dir>
set -eu

KEY_DIR="${1:?usage: ensure-jwt-keys.sh <key_dir> <public_dir>}"
PUBLIC_DIR="${2:?usage: ensure-jwt-keys.sh <key_dir> <public_dir>}"

umask 077
mkdir -p "$KEY_DIR" "$PUBLIC_DIR"

if [ -s "$KEY_DIR/private.pem" ]; then
    echo "[jwt-keys] keeping existing private key"
else
    # Write to a temp file first: an interrupted run never leaves a
    # half-written private.pem that the next run would keep.
    openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 \
        -out "$KEY_DIR/private.pem.tmp"
    mv "$KEY_DIR/private.pem.tmp" "$KEY_DIR/private.pem"
    echo "[jwt-keys] generated a new P-256 key pair"
fi
chmod 600 "$KEY_DIR/private.pem"

# Derived every run, so the public key can never drift from the private key.
openssl pkey -in "$KEY_DIR/private.pem" -pubout -out "$KEY_DIR/public.pem.tmp"
chmod 644 "$KEY_DIR/public.pem.tmp"
cp "$KEY_DIR/public.pem.tmp" "$PUBLIC_DIR/public.pem.tmp"
chmod 644 "$PUBLIC_DIR/public.pem.tmp"  # cp applied the 077 umask
mv "$KEY_DIR/public.pem.tmp" "$KEY_DIR/public.pem"
mv "$PUBLIC_DIR/public.pem.tmp" "$PUBLIC_DIR/public.pem"

echo "[jwt-keys] public key fingerprint (sha256 of DER):"
openssl pkey -pubin -in "$PUBLIC_DIR/public.pem" -outform DER | sha256sum
