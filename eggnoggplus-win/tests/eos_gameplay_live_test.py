"""Guarded, opt-in test of real Yule GGPO packets over real EOS on Windows.

Uses two local test accounts and a deterministic fixture instead of launching
the game. Tests connection, state/palette exchange, full confirmed histories,
HMAC/replay rejection and correction. This does not prove real-game rendering
or performance across distinct ISPs. Credentials/SDK output remain private.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import ssl
import subprocess
import sys
import time

from prematch_net_test import ROOT, assert_runtime_dlls, child_environment, find_gcc, GOOD_TOKEN


def account_login(args, directory, name, probe, env):
    path = directory / (name + ".json")
    new = not path.exists()
    account = ({"username": "eos_test_" + secrets.token_hex(4),
                "password": secrets.token_urlsafe(24)} if new else json.loads(path.read_text()))
    if new:
        path.write_text(json.dumps(account), encoding="utf-8")
    raw = socket.create_connection((args.host, args.port), timeout=25)
    conn = (ssl.create_default_context().wrap_socket(raw, server_hostname=args.server_name or args.host)
            if args.tls else raw)
    with conn:
        with conn.makefile("rwb") as wire:
            def send(message):
                wire.write(json.dumps(message).encode() + b"\n")
                wire.flush()

            def receive(kind):
                while True:
                    line = wire.readline(65537)
                    if not line or len(line) > 65536:
                        raise RuntimeError("Local server closed the test connection.")
                    msg = json.loads(line)
                    if msg.get("type") in ("error", "auth_fail"):
                        raise RuntimeError("Local server rejected test authentication.")
                    if msg.get("type") == kind:
                        return msg

            info = receive("server_info")
            if info.get("release_channel", "stable") != args.channel or info.get("p2p_protocol") != 18:
                raise RuntimeError("Wrong server channel/protocol; no credentials submitted.")
            send({"type": "register" if new else "login", "release_channel": args.channel,
                  "username": account["username"], "password": account["password"]})
            receive("auth_ok")
            send({"type": "eos_connect_token_request"})
            token = receive("eos_connect_token")["token"]
            result = subprocess.run([str(probe)], input=token + "\n", text=True,
                                    capture_output=True, cwd=directory, env=env, timeout=30)
            proofs = re.findall(r"^YULE_EOS_PROOF:([0-9a-f]{32}) ([A-Za-z0-9_.-]+)$",
                                result.stdout, re.MULTILINE)
            if result.returncode or len(proofs) != 1:
                raise RuntimeError("EOS Connect test account bootstrap failed.")
            puid, jwt = proofs[0]
            if account.get("expected_puid", puid) != puid:
                raise RuntimeError("Test account PUID changed.")
            send({"type": "eos_puid_proof", "puid": puid, "id_token": jwt})
            if receive("eos_puid_verified")["puid"] != puid:
                raise RuntimeError("Yule verified a different PUID.")
            account["expected_puid"] = puid
            path.write_text(json.dumps(account), encoding="utf-8")
            # Each fixture process logs in again with a fresh short-lived token.
            send({"type": "eos_connect_token_request"})
            return puid, receive("eos_connect_token")["token"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--port", type=int, default=47778)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--server-name")
    parser.add_argument("--channel", choices=("stable", "beta"), default="stable")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--route", choices=("relay", "auto"), default="relay")
    parser.add_argument("--case", choices=("chaos", "correction", "longcorrection", "reject", "disconnect"), default="chaos")
    parser.add_argument("--sdk", type=Path, default=Path(r"C:\Users\potato\Downloads\eos-sdk-1.18.1.2-minimal"))
    args = parser.parse_args()
    if not args.live or os.name != "nt":
        raise RuntimeError("Use --live on Windows to authorize local test accounts and EOS traffic.")
    if not args.tls and args.host not in ("127.0.0.1", "::1", "localhost"):
        raise RuntimeError("Remote test control connections require --tls.")
    gcc = find_gcc()
    assert_runtime_dlls(gcc)
    env = child_environment(gcc)
    secret = (ROOT / "eos_client_secret.txt").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[A-Za-z0-9_-]{16,256}", secret):
        raise RuntimeError("Restricted EOS client credential missing.")
    env["YULE_TEST_EOS_CLIENT_SECRET"] = secret
    output = args.output or ROOT / "build" / "eos_gameplay"
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.sdk / "Bin" / "EOSSDK-Win32-Shipping.dll", output)
    probe = output / "eos_connect_probe.exe"
    executable = output / "eos_gameplay_test.exe"
    common = [gcc, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror", "-DYULE_ENABLE_EOS",
              "-isystem", str(args.sdk / "Include"), "-I", str(ROOT)]
    subprocess.run(common + ["tests/eos_connect_probe.c", "eos_runtime.c", "-o", str(probe)],
                   cwd=ROOT, env=env, check=True, timeout=30)
    subprocess.run(common + ["-DGGPO_NET_TEST", "-DYULE_EOS_LIVE_TEST",
                   "tests/prematch_net_test.c", "ggpo_net.c", "ggpo_transport_native.c",
                   "ggpo_transport_eos.c", "eos_runtime.c", "fp_control.c",
                   "-lws2_32", "-lbcrypt", "-o", str(executable)],
                   cwd=ROOT, env=env, check=True, timeout=30)
    identities = [account_login(args, output, name, probe, env) for name in ("host", "join")]
    if identities[0][0] == identities[1][0]:
        raise RuntimeError("Test requires distinct stable EOS identities.")
    processes = []
    started = time.monotonic()
    try:
        socket_name = "YuleTest" + secrets.token_hex(8)
        for index, role in enumerate(("host", "join")):
            child = env.copy()
            child["YULE_TEST_EOS_LOCAL_PUID"], child["YULE_TEST_EOS_ACCESS_TOKEN"] = identities[index]
            child["YULE_TEST_EOS_PEER_PUID"] = identities[1 - index][0]
            child["YULE_TEST_EOS_SOCKET"] = socket_name
            if args.route == "relay":
                child["YULE_TEST_EOS_FORCE_RELAY"] = "1"
            child["EGGNOGGPLUS_TEST_STATE_TRACE"] = str(output / (role + ".trace"))
            command = [str(executable), role, str(1 + index), str(2 - index), GOOD_TOKEN, args.case,
                       "tamper" if args.case == "reject" else "replay"]
            processes.append(subprocess.Popen(command, cwd=ROOT, env=child, text=True,
                                              stdout=subprocess.PIPE, stderr=subprocess.PIPE))
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda process: process.communicate(timeout=240), processes))
        for index, (stdout, stderr) in enumerate(results):
            # Only fixture/transport diagnostics, never raw SDK credential output.
            safe = "\n".join(line for line in (stdout + stderr).splitlines()
                             if re.match(r"^(host |join |\[(INFO|WARN|ERROR|DEBUG)\] ggpo\.)", line))
            (output / (f"{args.route}_{args.case}_{index}.log")).write_text(safe, encoding="utf-8")
        for index, (stdout, stderr) in enumerate(results):
            if processes[index].returncode:
                raise RuntimeError(f"EOS {args.route}/{args.case} fixture {index} failed (exit {processes[index].returncode}); see sanitized build/eos_gameplay log.")
            if args.route == "relay" and args.case != "reject" and "route=relay" not in stdout + stderr:
                raise RuntimeError("EOS did not confirm relay routing.")
        if args.case == "chaos":
            pattern = r"CHAOS PASS start=(\d+) target=(\d+) wrapped=\d+ checksum=(\d+)"
            summaries = [re.search(pattern, result[0]) for result in results]
            if not all(summaries) or summaries[0].groups() != summaries[1].groups():
                raise RuntimeError("Confirmed EOS gameplay checksums differ.")
            traces = [(output / (name + ".trace")).read_bytes() for name in ("host", "join")]
            if not traces[0] or traces[0] != traces[1]:
                raise RuntimeError("Exact confirmed state histories differ across EOS.")
        if args.case in ("correction", "longcorrection"):
            summaries = [re.search(r"CORRECTION PASS long=\d+ target=(\d+) checksum=(\d+)", result[0]) for result in results]
            if not all(summaries) or summaries[0].groups() != summaries[1].groups():
                raise RuntimeError("EOS correction did not converge to the same confirmed state.")
        if args.case == "reject" and any("REJECT PASS rejected=0" in result[0] for result in results):
            raise RuntimeError("EOS tampered payloads were not rejected by Yule HMAC.")
        print(f"EOS {args.route}/{args.case}: PASS, two distinct PUIDs, elapsed={time.monotonic() - started:.1f}s")
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
