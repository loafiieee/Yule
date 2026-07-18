"""Build and adversarially exercise the authenticated v16 prematch transport."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SOURCE = ROOT / "tests" / "prematch_net_test.c"
EXE = BUILD / "prematch_net_test.exe"
GOOD_TOKEN = "00112233445566778899aabbccddeefffedcba98765432100123456789abcdef"
BAD_TOKEN = "ffeeddccbbaa998877665544332211000123456789abcdef0123456789abcdef"


def find_gcc() -> str:
    configured = os.environ.get("CC")
    if configured:
        return configured
    found = shutil.which("gcc")
    if found:
        return found
    mingw = Path(r"C:\msys64\mingw32\bin\gcc.exe")
    if mingw.exists():
        return str(mingw)
    raise RuntimeError("32-bit MinGW gcc was not found; set CC to its path")


def reserve_port_pair() -> tuple[int, int]:
    sockets: list[socket.socket] = []
    try:
        for _ in range(2):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(("127.0.0.1", 0))
            sockets.append(sock)
        return sockets[0].getsockname()[1], sockets[1].getsockname()[1]
    finally:
        for sock in sockets:
            sock.close()


def build() -> None:
    BUILD.mkdir(exist_ok=True)
    gcc = find_gcc()
    command = [
        gcc,
        "-m32",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DGGPO_NET_TEST",
        str(SOURCE),
        str(ROOT / "ggpo_net.c"),
        f"-I{ROOT}",
        "-lws2_32",
        "-lbcrypt",
        "-o",
        str(EXE),
    ]
    env = os.environ.copy()
    gcc_dir = str(Path(gcc).resolve().parent)
    env["PATH"] = gcc_dir + os.pathsep + env.get("PATH", "")
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def run_pair(
    join_role: str,
    expect_recovery: bool,
    *,
    host_token: str = GOOD_TOKEN,
    join_token: str = GOOD_TOKEN,
    expect_rejection: bool = False,
    tamper_host: bool = False,
    replay_host: bool = False,
    require_host_rejects: bool = False,
    require_join_rejects: bool = False,
) -> None:
    host_port, join_port = reserve_port_pair()
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    mode = "reject" if expect_rejection else "normal"
    host_args = [str(EXE), "host", str(host_port), str(join_port), host_token, mode]
    if tamper_host:
        host_args.append("tamper")
    elif replay_host:
        host_args.append("replay")
    join_args = [str(EXE), join_role, str(join_port), str(host_port), join_token, mode]
    host = subprocess.Popen(
        host_args,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=creationflags,
    )
    join = subprocess.Popen(
        join_args,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=creationflags,
    )
    try:
        host_out, host_err = host.communicate(timeout=30)
        join_out, join_err = join.communicate(timeout=30)
    except BaseException:
        host.kill()
        join.kill()
        host.wait()
        join.wait()
        raise

    if host.returncode != 0 or join.returncode != 0:
        raise AssertionError(
            f"prematch pair failed ({host.returncode=}, {join.returncode=})\n"
            f"HOST OUT:\n{host_out}\nHOST ERR:\n{host_err}\n"
            f"JOIN OUT:\n{join_out}\nJOIN ERR:\n{join_err}"
        )
    expected_marker = "REJECT PASS" if expect_rejection else " PASS"
    if expected_marker not in host_out or expected_marker not in join_out:
        raise AssertionError(f"missing PASS output\n{host_out}\n{join_out}")
    if expect_rejection or require_host_rejects or require_join_rejects:
        host_match = re.search(r"rejected=(\d+)", host_out)
        join_match = re.search(r"rejected=(\d+)", join_out)
        if not host_match or not join_match:
            raise AssertionError(f"missing authentication counters\n{host_out}\n{join_out}")
        if require_host_rejects and int(host_match.group(1)) == 0:
            raise AssertionError(f"host did not reject any forged packets\n{host_out}")
        if require_join_rejects and int(join_match.group(1)) == 0:
            raise AssertionError(f"join did not reject any forged packets\n{join_out}")
    if expect_recovery:
        if "injected one-shot load failure" not in join_err:
            raise AssertionError("the one-shot state-load failure was not exercised")
        if join_err.count("receiving host state") < 2:
            raise AssertionError("the failed state assembly was not retried")
    print(host_out.strip())
    print(join_out.strip())


def main() -> int:
    if os.name != "nt":
        print("prematch_net_test: skipped (Windows/WinSock test)")
        return 0
    try:
        build()
        no_key = subprocess.run(
            [str(EXE), "nokey"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
        )
        if no_key.returncode != 0 or "nokey PASS" not in no_key.stdout:
            raise AssertionError(
                f"missing-key fail-closed test failed\nOUT:\n{no_key.stdout}\nERR:\n{no_key.stderr}"
            )
        print(no_key.stdout.strip())
        run_pair("join", expect_recovery=False)
        run_pair(
            "join",
            expect_recovery=False,
            replay_host=True,
            require_join_rejects=True,
        )
        run_pair("joinfail", expect_recovery=True)
        run_pair(
            "join",
            expect_recovery=False,
            join_token=BAD_TOKEN,
            expect_rejection=True,
            require_host_rejects=True,
            require_join_rejects=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            expect_rejection=True,
            tamper_host=True,
            require_join_rejects=True,
        )
        print("prematch_net_test: all checks passed")
    finally:
        try:
            EXE.unlink()
        except FileNotFoundError:
            pass
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, AssertionError) as exc:
        print(f"prematch_net_test: FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
