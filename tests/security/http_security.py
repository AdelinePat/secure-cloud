import argparse
import base64
import http.client
import json
import secrets
import subprocess
import time
import uuid
from pathlib import Path

COMPOSE_FILE = Path(__file__).with_name("compose.yml")


def compose(project, *args, **kwargs):
    return subprocess.run(
        ["docker", "compose", "-f", str(COMPOSE_FILE), "-p", project, *args],
        check=kwargs.pop("check", True),
        text=kwargs.pop("text", True),
        **kwargs,
    )


def request(port, method, path, expected, *, token=None, data=None, allow=None):
    headers = {"Content-Type": "application/json"}
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        body = None if data is None else json.dumps(data)
        connection.request(method, path, body, headers)
        response = connection.getresponse()
        payload = response.read()
        if response.status != expected:
            raise RuntimeError(f"{method} {path}: HTTP {response.status}, expected {expected}")
        if allow is not None and response.getheader("Allow") != allow:
            raise RuntimeError(f"{method} {path}: expected Allow {allow}")
        return json.loads(payload) if payload else None
    finally:
        connection.close()


def wait_for_services(project, port):
    deadline = time.monotonic() + 900
    while time.monotonic() < deadline:
        try:
            request(port, "GET", "/auth/health", 200)
            request(port, "GET", "/messaging/health", 200)
            return
        except (OSError, http.client.HTTPException, RuntimeError):
            running = compose(project, "ps", "--status", "running", "-q", "backend",
                              capture_output=True)
            if not running.stdout.strip():
                raise RuntimeError("Backend exited before becoming ready") from None
            time.sleep(2)
    raise RuntimeError("Backend health check timed out")


def seed_and_login(project, port, name):
    username = f"{name}_test"
    password = secrets.token_urlsafe(32)
    compose(project, "exec", "-T", "backend", "bash", "-c",
            'IFS= read -r AUTH_TEST_PASSWORD; export AUTH_TEST_PASSWORD; '
            'exec /tmp/build-auth/bin/seed_test_user "$1"', "seed", username,
            text=False, input=(password + "\n").encode("ascii"),
            stdout=subprocess.DEVNULL)
    session = request(port, "POST", "/auth/login", 200,
                      data={"username": username, "password": password})
    return session["access_token"], session["user_id"]


def encoded_random(count):
    return base64.b64encode(secrets.token_bytes(count)).decode("ascii")


def check(condition, description):
    if not condition:
        raise RuntimeError(description)


def run(project):
    try:
        compose(project, "up", "--build", "-d")
        binding = compose(project, "port", "backend", "8080", capture_output=True)
        port = int(binding.stdout.strip().rsplit(":", 1)[1])
        wait_for_services(project, port)

        alice, alice_id = seed_and_login(project, port, "alice")
        bob, bob_id = seed_and_login(project, port, "bob")
        charlie, _ = seed_and_login(project, port, "charlie")

        conversations = "/messaging/conversations"
        identity = "/messaging/identity-key"
        request(port, "GET", conversations, 401)
        request(port, "GET", conversations, 401, token="invalid")

        group = request(port, "POST", conversations, 201, token=alice,
                        data={"type": "group", "name": "Security tests",
                              "participants": ["bob_test"]})
        messages = f"{conversations}/{group['id']}/messages"
        peer = f"{conversations}/{group['id']}/peer-key"
        request(port, "GET", messages, 403, token=charlie)
        request(port, "POST", messages, 403, token=charlie, data={})
        request(port, "GET", peer, 403, token=charlie)
        for token in (alice, bob):
            entries = request(port, "GET", conversations, 200, token=token)
            check([entry["id"] for entry in entries] == [group["id"]],
                  "Missing conversation for participant")
        check(request(port, "GET", conversations, 200, token=charlie) == [],
              "Conversation exposed to a non-participant")

        message_id = str(uuid.uuid4())
        request(port, "POST", messages, 201, token=alice,
                data={"message_id": message_id, "user_id": bob_id,
                      "nonce": encoded_random(24), "ciphertext": encoded_random(32),
                      "encryption_version": 2})
        history = request(port, "GET", messages, 200, token=bob)["messages"]
        check(len(history) == 1 and history[0]["message_id"] == message_id and
              history[0]["user_id"] == alice_id, "Incorrect sender in message history")
        check(history[0].get("sender_username") == "alice_test",
            "Incorrect sender username in message history")

        key = {"public_key": encoded_random(32)}
        request(port, "PUT", identity, 200, token=alice, data=key)
        request(port, "PUT", identity, 200, token=alice, data=key)
        request(port, "PUT", identity, 409, token=alice,
                data={"public_key": encoded_random(32)})
        check(request(port, "GET", peer, 200, token=bob) ==
              {"user_id": alice_id, **key}, "Stored identity key changed")

        invalid = f"{conversations}/invalid"
        request(port, "GET", invalid + "/peer-key", 400, token=alice)
        request(port, "GET", invalid + "/messages", 400, token=alice)
        request(port, "POST", invalid + "/messages", 400, token=alice, data={})
        for path, allow in ((identity, "PUT"), (peer, "GET"),
                            (messages, "GET, POST"), (conversations, "GET, POST")):
            request(port, "DELETE", path, 405, allow=allow)
        request(port, "GET", "/messaging/health", 200)
        print("HTTP integration checks passed.")
    except Exception:
        compose(project, "logs", "--tail", "50", "backend", check=False)
        raise
    finally:
        compose(project, "down", check=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.parse_args()
    try:
        run("securechat-security-" + uuid.uuid4().hex[:12])
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
