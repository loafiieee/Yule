"""Opt-in live EOS test: local Yule login -> UserInfo -> Connect -> PUID proof.

Creates one reusable local test account, stored only in ignored build/eos_*.
Captures SDK output privately so ID tokens and credentials never reach logs.
"""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--port", type=int, default=47778)
    parser.add_argument("--sdk", type=Path, default=Path(r"C:\Users\potato\Downloads\eos-sdk-1.18.1.2-minimal"))
    args = parser.parse_args()
    if not args.live or os.name != "nt":
        raise RuntimeError("Use --live on Windows to opt in to account creation and EOS requests.")
    env = os.environ.copy()
    search = [ROOT, Path(r"C:\msys64\mingw32\bin"), Path(r"C:\msys64\usr\bin")]
    for name in ("libgcc_s_dw2-1.dll", "libwinpthread-1.dll"):
        if not any((directory / name).is_file() for directory in search):
            raise RuntimeError("Required MinGW runtime is missing; refusing to launch.")
    env["PATH"] = os.pathsep.join(map(str, search)) + os.pathsep + env.get("PATH", "")
    compiler = shutil.which("gcc", path=env["PATH"])
    if not compiler:
        raise RuntimeError("32-bit MinGW GCC was not found.")
    output = ROOT / "build" / "eos_local_connect"
    output.mkdir(parents=True, exist_ok=True)
    executable = output / "eos_connect_probe.exe"
    shutil.copy2(args.sdk / "Bin" / "EOSSDK-Win32-Shipping.dll", output)
    subprocess.run([compiler, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-DYULE_ENABLE_EOS", "-isystem", str(args.sdk / "Include"),
                    str(ROOT / "tests" / "eos_connect_probe.c"), str(ROOT / "eos_runtime.c"),
                    "-o", str(executable)], env=env, check=True, timeout=30)
    credential = (ROOT / "eos_client_secret.txt").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[A-Za-z0-9_-]{16,256}", credential):
        raise RuntimeError("Restricted client credential is unavailable.")
    env["YULE_TEST_EOS_CLIENT_SECRET"] = credential
    account_file = output / "test_account.json"
    new_account = not account_file.exists()
    if new_account:
        account = {"username": "eos_local_" + secrets.token_hex(4), "password": secrets.token_urlsafe(24)}
        account_file.write_text(json.dumps(account), encoding="utf-8")
    else:
        account = json.loads(account_file.read_text(encoding="utf-8"))
    with socket.create_connection(("127.0.0.1", args.port), timeout=20) as conn:
        with conn.makefile("rwb") as wire:
            def send(message):
                wire.write(json.dumps(message).encode("utf-8") + b"\n")
                wire.flush()

            def receive(kind):
                while True:
                    line = wire.readline(65537)
                    if not line or len(line) > 65536:
                        raise RuntimeError("Local server connection closed or oversized response.")
                    message = json.loads(line)
                    if message.get("type") in ("error", "auth_fail"):
                        raise RuntimeError("Local server rejected the EOS test request.")
                    if message.get("type") == kind:
                        return message

            send({"type": "register" if new_account else "login",
                  "username": account["username"], "password": account["password"]})
            receive("auth_ok")
            send({"type": "eos_connect_token_request"})
            token = receive("eos_connect_token")["token"]
            probe = subprocess.run([str(executable)], input=token + "\n", text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=env, timeout=30, cwd=output)
            if probe.returncode:
                raise RuntimeError("Windows EOS Connect login did not complete.")
            proofs = re.findall(r"^YULE_EOS_PROOF:([0-9a-f]{32}) ([A-Za-z0-9_.-]+)$",
                                probe.stdout, re.MULTILINE)
            if len(proofs) != 1:
                raise RuntimeError("EOS Connect did not return a single signed proof.")
            puid, jwt = proofs[0]
            if account.get("expected_puid") and account["expected_puid"] != puid:
                raise RuntimeError("EOS PUID changed for the same immutable Yule account.")
            send({"type": "eos_puid_proof", "puid": puid, "id_token": jwt})
            verified = receive("eos_puid_verified")
            if verified.get("puid") != puid:
                raise RuntimeError("Local server accepted a mismatched PUID.")
            account["expected_puid"] = puid
            account_file.write_text(json.dumps(account), encoding="utf-8")
    print("Local Yule account -> public UserInfo -> Windows EOS Connect -> Windows server PUID verification: PASS")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print("EOS local test failed: " + str(error), file=sys.stderr)
        sys.exit(1)
