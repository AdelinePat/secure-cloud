#!/usr/bin/env bash
set -euo pipefail

umask 077
mkdir -p /tmp/securechat-jwt
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 \
    -out "$AUTH_JWT_PRIVATE_KEY_PATH"
openssl pkey -in "$AUTH_JWT_PRIVATE_KEY_PATH" -pubout \
    -out "$AUTH_JWT_PUBLIC_KEY_PATH"

export SECURECHAT_HISTORY_TEST_DB="$AUTH_DATABASE_URL"
for service in auth messaging gateway; do
    cmake -S "/app/$service" -B "/tmp/build-$service" -G Ninja
    cmake --build "/tmp/build-$service" --parallel 2
    ctest --test-dir "/tmp/build-$service" --output-on-failure
done

/tmp/build-auth/bin/auth &
/tmp/build-messaging/bin/messaging &
/tmp/build-gateway/bin/gateway &
wait -n
