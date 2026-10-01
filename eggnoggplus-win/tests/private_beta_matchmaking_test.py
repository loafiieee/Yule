"""Opt-in checks against the owner's private TLS beta, with guarded EOS probes."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import ssl
import subprocess
import time

from prematch_net_test import ROOT, assert_runtime_dlls, child_environment, find_gcc


class Client:
    def __init__(self):
        raw = socket.create_connection(("beta.loafiieee.com", 47782), timeout=20)
        self.socket = ssl.create_default_context().wrap_socket(raw, server_hostname="beta.loafiieee.com")
        self.wire = self.socket.makefile("rwb")
        info = self.receive("server_info")
        if info.get("release_channel") != "beta" or info.get("p2p_protocol") != 18:
            self.close()
            raise RuntimeError("Wrong server; no credentials submitted")

    def send(self, message):
        self.wire.write(json.dumps(message).encode() + b"\n")
        self.wire.flush()

    def receive(self, kind):
        deadline = time.monotonic() + 45
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("Timed out waiting for " + kind)
            self.socket.settimeout(remaining)
            line = self.wire.readline(65537)
            if not line or len(line) > 65536:
                raise RuntimeError("Server closed or oversized test response")
            message = json.loads(line)
            if message.get("type") == kind:
                return message
            if message.get("type") in ("error", "auth_fail"):
                raise RuntimeError("Unexpected server rejection during private beta test")

    def close(self):
        self.wire.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args()
    if not args.live or os.name != "nt":
        raise RuntimeError("Use --live on Windows after the private beta gameplay bootstrap")
    output = ROOT / "build/private_beta/eos_gameplay"
    accounts = [json.loads((output / (name + ".json")).read_text()) for name in ("host", "join")]
    gcc = find_gcc()
    assert_runtime_dlls(gcc)
    env = child_environment(gcc)
    env["YULE_TEST_EOS_CLIENT_SECRET"] = (ROOT / "eos_client_secret.txt").read_text().strip()
    probe = output / "eos_connect_probe.exe"
    if not probe.exists() or not (output / "EOSSDK-Win32-Shipping.dll").exists():
        raise RuntimeError("Guarded EOS probe/runtime missing")
    proofs = []
    for caps, expected in [((3, 3), "eos_p2p"), ((3, 1), "native_udp"), ((2, 1), None)]:
        clients = [Client(), Client()]
        try:
            for client, account in zip(clients, accounts):
                client.send({"type": "login", "release_channel": "beta",
                             "username": account["username"], "password": account["password"]})
                client.receive("auth_ok")
            if not proofs:
                for client, account in zip(clients, accounts):
                    client.send({"type": "eos_connect_token_request"})
                    token = client.receive("eos_connect_token")["token"]
                    result = subprocess.run([str(probe)], input=token + "\n", text=True,
                                            capture_output=True, env=env, cwd=output, timeout=30)
                    found = re.findall(r"^YULE_EOS_PROOF:([0-9a-f]{32}) ([A-Za-z0-9_.-]+)$",
                                       result.stdout, re.M)
                    if result.returncode or len(found) != 1 or found[0][0] != account["expected_puid"]:
                        raise RuntimeError("EOS identity bootstrap changed or failed")
                    proofs.append(found[0])
                # A valid Epic-signed token from the other Yule account must fail.
                clients[0].send({"type": "eos_puid_proof", "puid": proofs[1][0], "id_token": proofs[1][1]})
                rejection = clients[0].receive("error")
                if "identity" not in rejection.get("message", "").lower():
                    raise RuntimeError("Unexpected response to opponent identity substitution")
                print("Matchmaking rejects another account's signed EOS identity: PASS")
            for client, proof in zip(clients, proofs):
                client.send({"type": "eos_puid_proof", "puid": proof[0], "id_token": proof[1]})
                if client.receive("eos_puid_verified").get("puid") != proof[0]:
                    raise RuntimeError("Verified identity mismatch")
            for client, cap in zip(clients, caps):
                client.send({"type": "map_manifest", "framework_version": "2.0.0",
                             "control_protocol": 3, "match_protocol": 4, "p2p_protocol": 18,
                             "build_id": 0x1234ABCD, "game_exe_id": 0x2345BCDE,
                             "framework_dll_id": 0x3456CDEF, "transport_caps": cap,
                             "p2p_port": 0, "maps": []})
                client.send({"type": "join_queue", "queue": "casual"})
                client.receive("queue_joined")
            if expected:
                matches = [client.receive("match_found") for client in clients]
                if any(match.get("transport") != expected for match in matches):
                    raise RuntimeError("Wrong preferred/fallback transport")
                if matches[0]["match_id"] != matches[1]["match_id"]:
                    raise RuntimeError("Match identities disagree")
                if expected == "eos_p2p":
                    for index, match in enumerate(matches):
                        if match.get("peer_eos_puid") != proofs[1 - index][0]:
                            raise RuntimeError("Matchmaking supplied wrong opponent PUID")
                    if not matches[0].get("eos_socket") or matches[0]["eos_socket"] != matches[1]["eos_socket"]:
                        raise RuntimeError("EOS socket identity missing or inconsistent")
                print("Actual beta matchmaking " + str(caps) + " -> " + expected + ": PASS")
            else:
                rejection = clients[0].receive("error")
                if "compatible gameplay transport" not in rejection.get("message", ""):
                    raise RuntimeError("EOS-only/native-only pair was not rejected")
                print("EOS-only/native-only pairing rejected: PASS")
        finally:
            for client in clients:
                client.close()
        time.sleep(.2)


if __name__ == "__main__":
    main()
