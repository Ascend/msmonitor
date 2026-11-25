"""Verify staged credential rotation across two I8 replicas without changing credentials."""

import argparse
import itertools
import json
import re
import secrets
import ssl
from pathlib import Path

from test_control_service import (
    HEARTBEAT,
    READY,
    REGISTER,
    Client,
    check_heartbeat,
    heartbeat_payload,
    registered_session,
    service_origin,
    validate_replica_arguments,
    wait_until,
)


TOKEN = re.compile(r"[A-Za-z0-9._~+/=-]{32,256}")


def read_test_credentials(path, scope):
    credentials = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(credentials, dict):
        raise ValueError("The dual-token file must map scopes to credentials")
    if scope is None:
        scope = next((key for key, value in credentials.items() if isinstance(value, list)), None)
    rotating = credentials.get(scope)
    other = next((value for key, value in credentials.items() if key != scope and isinstance(value, str)), None)
    if not isinstance(rotating, list) or len(rotating) != 2:
        raise ValueError("The rotating scope must contain [old_token, new_token]")
    tokens = (*rotating, other)
    if not all(isinstance(token, str) and TOKEN.fullmatch(token) for token in tokens):
        raise ValueError("Provide two valid rotating tokens and one valid token for another scope")
    if len(set(tokens)) != 3:
        raise ValueError("The three credentials must be distinct")
    return tokens


def verify_rotation(a, b, old, new, other, timeout):
    identity = {"daemon_id": "i8-rotate-" + secrets.token_hex(8), "instance_id": "boot-1"}
    sequence = itertools.count(1)
    for replica in (a, b):
        replica.expect(READY)
    original = registered_session(a, old, identity)
    isolated = registered_session(a, other, identity)
    if isolated == original:
        raise AssertionError("Different scopes were assigned the same session")

    def verify_replica(replica, allowed):
        for token in (old, new):
            if token not in allowed:
                replica.expect(REGISTER, status=401, token=token, data=identity)
                replica.expect(HEARTBEAT, status=401, token=token, data=heartbeat_payload(original, next(sequence)))
                continue
            if registered_session(replica, token, identity) != original:
                raise AssertionError("The rotating scope lost its session")
            check_heartbeat(replica, token, original, next(sequence))
            replica.expect(HEARTBEAT, status=403, token=token, data=heartbeat_payload(isolated, next(sequence)))
        if registered_session(replica, other, identity) != isolated:
            raise AssertionError("The other scope lost its session")
        check_heartbeat(replica, other, isolated, next(sequence))
        replica.expect(HEARTBEAT, status=403, token=other, data=heartbeat_payload(original, next(sequence)))

    def transition(instruction, updated, sibling, sibling_token, probe_token, status):
        print(instruction, flush=True)

        def probe():
            check_heartbeat(sibling, sibling_token, original, next(sequence))
            try:
                if updated.request(READY)[0] != 200:
                    return False
                return updated.request(REGISTER, token=probe_token, data=identity)[0] == status
            except ssl.SSLError:
                raise
            except OSError:
                return False

        wait_until(probe, timeout, f"credential change on {updated.origin.netloc}")

    verify_replica(a, (old,))
    verify_replica(b, (old,))
    print("PASS: both replicas accept only the old credential", flush=True)

    transition("Install the dual-token file, then recreate app1 only.", a, b, old, new, 200)
    verify_replica(a, (old, new))
    verify_replica(b, (old,))
    print("PASS: app1 accepts both credentials while app2 still accepts only the old one", flush=True)

    transition("Recreate app2 with the same dual-token file.", b, a, old, new, 200)
    verify_replica(a, (old, new))
    verify_replica(b, (old, new))
    print("PASS: both replicas accept both credentials for the original session", flush=True)

    transition("Install the new-only token file, then recreate app1 only.", a, b, new, old, 401)
    verify_replica(a, (new,))
    verify_replica(b, (old, new))
    print("PASS: app1 rejects the old credential while app2 still accepts both", flush=True)

    transition("Recreate app2 with the same new-only token file.", b, a, new, old, 401)
    verify_replica(a, (new,))
    verify_replica(b, (new,))
    print("PASS: both replicas accept only the new credential and retained scope isolation", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin-a", required=True, type=service_origin)
    parser.add_argument("--origin-b", required=True, type=service_origin)
    parser.add_argument(
        "--tokens", required=True, type=Path, help="Dual-token file kept separate from the active credential file"
    )
    parser.add_argument("--scope", help="Rotating scope, defaults to the first scope with a token list")
    parser.add_argument("--ca", type=Path, help="CA certificate for HTTPS origins")
    parser.add_argument(
        "--transition-timeout", type=int, default=180, help="Seconds to wait for each manual rollout step"
    )
    args = parser.parse_args()
    validate_replica_arguments(parser, args)
    try:
        old, new, other = read_test_credentials(args.tokens, args.scope)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    tls = ssl.create_default_context(cafile=str(args.ca) if args.ca else None)
    verify_rotation(Client(args.origin_a, tls), Client(args.origin_b, tls), old, new, other, args.transition_timeout)


if __name__ == "__main__":
    main()
