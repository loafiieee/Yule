#!/usr/bin/env python3
"""Verify that a deployed Eggnogg+ server can issue authenticated P2P matches."""

from __future__ import annotations

import argparse
import json
import socket
import sys


REQUIRED_CONTROL_PROTOCOL = 2
REQUIRED_MATCH_PROTOCOL = 2
REQUIRED_P2P_PROTOCOL = 16
MAX_REPLY_BYTES = 8192
UDP_PROBE_SEQUENCE = 0x45504750


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Probe the public, unauthenticated Eggnogg+ server-info endpoint."
    )
    parser.add_argument("host", nargs="?", default="eggnogg.loafiieee.com")
    parser.add_argument("port", nargs="?", type=int, default=47778)
    parser.add_argument("--timeout", type=float, default=5.0)
    return parser.parse_args()


def read_server_info(sock: socket.socket) -> dict[str, object]:
    # New servers send this as their welcome. Request it as well so the probe
    # remains reliable if welcome timing/policy changes later.
    sock.sendall(b'{"type":"server_info"}\n')
    data = bytearray()
    while len(data) < MAX_REPLY_BYTES:
        chunk = sock.recv(min(4096, MAX_REPLY_BYTES - len(data)))
        if not chunk:
            break
        data.extend(chunk)
        while b"\n" in data:
            raw, _, remainder = data.partition(b"\n")
            data = bytearray(remainder)
            if not raw.strip():
                continue
            try:
                message = json.loads(raw)
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise RuntimeError(f"server returned malformed JSON: {exc}") from exc
            if isinstance(message, dict) and message.get("type") == "server_info":
                return message
    raise RuntimeError(
        "server did not return server_info; the deployed process is likely outdated"
    )


def require_exact_int(info: dict[str, object], field: str, expected: int) -> None:
    value = info.get(field)
    if type(value) is not int or value != expected:
        raise RuntimeError(f"{field}={value!r}; required {expected}")


def check_udp(host: str, port: int, timeout: float) -> None:
    address = socket.gethostbyname(host)
    request = json.dumps(
        {"type": "udp_ping", "seq": UDP_PROBE_SEQUENCE}, separators=(",", ":")
    ).encode()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.sendto(request, (address, port))
        raw, _ = sock.recvfrom(2048)
    try:
        reply = json.loads(raw.strip())
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"UDP endpoint returned malformed JSON: {exc}") from exc
    if not isinstance(reply, dict) or reply.get("type") != "udp_pong":
        raise RuntimeError("UDP endpoint did not return udp_pong")
    if reply.get("seq") != UDP_PROBE_SEQUENCE:
        raise RuntimeError("UDP endpoint returned the wrong probe sequence")


def main() -> int:
    args = parse_args()
    if not 1 <= args.port <= 65535:
        raise RuntimeError("port must be in 1..65535")
    if not 0.1 <= args.timeout <= 60.0:
        raise RuntimeError("timeout must be in 0.1..60 seconds")

    with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
        sock.settimeout(args.timeout)
        info = read_server_info(sock)

    require_exact_int(info, "control_protocol", REQUIRED_CONTROL_PROTOCOL)
    require_exact_int(info, "match_protocol", REQUIRED_MATCH_PROTOCOL)
    require_exact_int(info, "p2p_protocol", REQUIRED_P2P_PROTOCOL)
    require_exact_int(info, "cap_p2p_auth", 1)
    check_udp(args.host, args.port, args.timeout)
    print(
        f"OK {args.host}:{args.port}: control v{REQUIRED_CONTROL_PROTOCOL}, "
        f"match v{REQUIRED_MATCH_PROTOCOL}, P2P v{REQUIRED_P2P_PROTOCOL}, "
        "packet authentication and UDP discovery available"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"DEPLOYMENT CHECK FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
