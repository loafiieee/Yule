"""Build and adversarially exercise the authenticated v17 prematch transport."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SOURCE = ROOT / "tests" / "prematch_net_test.c"
EXE = BUILD / "prematch_net_test.exe"
MINGW_BIN = Path(r"C:\msys64\mingw32\bin")
MSYS_BIN = Path(r"C:\msys64\usr\bin")
REQUIRED_RUNTIME_DLLS = ("libgcc_s_dw2-1.dll", "libwinpthread-1.dll")
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


def child_environment(gcc: str | None = None) -> dict[str, str]:
    env = os.environ.copy()
    prefixes = [str(MINGW_BIN), str(MSYS_BIN), str(ROOT)]
    if gcc:
        prefixes.insert(0, str(Path(gcc).resolve().parent))
    # Preserve order while avoiding duplicate PATH entries.
    unique_prefixes = list(dict.fromkeys(prefixes))
    env["PATH"] = os.pathsep.join(unique_prefixes + [env.get("PATH", "")])
    return env


def assert_runtime_dlls(gcc: str | None = None) -> None:
    search_directories = [ROOT, MINGW_BIN, MSYS_BIN]
    if gcc:
        search_directories.insert(0, Path(gcc).resolve().parent)
    missing = [
        name
        for name in REQUIRED_RUNTIME_DLLS
        if not any((directory / name).is_file() for directory in search_directories)
    ]
    if missing:
        joined = ", ".join(missing)
        raise RuntimeError(
            f"required test runtime DLL(s) not found: {joined}; "
            "refusing to launch Windows test executables"
        )


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


def reserve_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]
    finally:
        sock.close()


class UdpPairRelay:
    """Small symmetric relay used to exercise the production relay topology."""

    def __init__(self, port: int, host_port: int, join_port: int) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", port))
        self.sock.settimeout(0.1)
        self.host_port = host_port
        self.join_port = join_port
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.host_packets = 0
        self.join_packets = 0
        self.forwarded_packets = 0
        self.packet_types: dict[int, int] = {}
        self.socket_errors: list[str] = []
        self.unknown_sources: dict[int, int] = {}

    def start(self) -> None:
        self.thread.start()

    def close(self) -> None:
        self.stop_event.set()
        self.thread.join(timeout=2.0)
        self.sock.close()

    def _run(self) -> None:
        while not self.stop_event.is_set():
            try:
                payload, source = self.sock.recvfrom(2048)
            except TimeoutError:
                continue
            except OSError as exc:
                if self.stop_event.is_set():
                    return
                # Windows reports an ICMP "port unreachable" from an early
                # pre-bind datagram as WSAECONNRESET on a later recvfrom().
                # A long-lived UDP relay must survive that asynchronous error.
                self.socket_errors.append(str(exc))
                continue
            if source[0] != "127.0.0.1":
                continue
            destination = (
                self.join_port
                if source[1] == self.host_port
                else self.host_port
                if source[1] == self.join_port
                else 0
            )
            if destination:
                if source[1] == self.host_port:
                    self.host_packets += 1
                else:
                    self.join_packets += 1
                if len(payload) >= 8:
                    packet_type = struct.unpack_from("<H", payload, 6)[0]
                    self.packet_types[packet_type] = (
                        self.packet_types.get(packet_type, 0) + 1
                    )
                self.sock.sendto(payload, ("127.0.0.1", destination))
                self.forwarded_packets += 1
            else:
                self.unknown_sources[source[1]] = (
                    self.unknown_sources.get(source[1], 0) + 1
                )

    def summary(self) -> str:
        return (
            f"host_packets={self.host_packets} join_packets={self.join_packets} "
            f"forwarded={self.forwarded_packets} types={self.packet_types} "
            f"unknown={self.unknown_sources} socket_errors={self.socket_errors}"
        )


def valid_chaos_metadata(fields: dict[str, str]) -> bool:
    try:
        seed = int(fields.get("sim_seed", "0"), 16)
        total = int(fields.get("chaos_event_total", "-1"))
        count = int(fields.get("chaos_event_count", "-1"))
        events = sorted(
            (key, value)
            for key, value in fields.items()
            if key.startswith("chaos_event_")
            and key not in ("chaos_event_total", "chaos_event_count")
        )
        if seed == 0 or count <= 0 or total != count or len(events) != count:
            return False
        for index, (key, value) in enumerate(events):
            if key != f"chaos_event_{index:04d}":
                return False
            sequence, _tick, kind, packet_type, event_value = (
                int(part) for part in value.split(",")
            )
            if (
                sequence != index
                or kind not in (1, 2, 3)
                or not (1 <= packet_type <= 9)
                or event_value < 0
            ):
                return False
        return True
    except (TypeError, ValueError):
        return False


def build() -> dict[str, str]:
    BUILD.mkdir(exist_ok=True)
    gcc = find_gcc()
    assert_runtime_dlls(gcc)
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
        str(ROOT / "fp_control.c"),
        f"-I{ROOT}",
        "-lws2_32",
        "-lbcrypt",
        "-o",
        str(EXE),
    ]
    env = child_environment(gcc)
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    return env


def run_pair(
    join_role: str,
    expect_recovery: bool,
    child_env: dict[str, str],
    *,
    host_token: str = GOOD_TOKEN,
    join_token: str = GOOD_TOKEN,
    expect_rejection: bool = False,
    tamper_host: bool = False,
    replay_host: bool = False,
    require_host_rejects: bool = False,
    require_join_rejects: bool = False,
    restart_join: bool = False,
    restart_host_late: bool = False,
    final_layout: bool = False,
    layout_mismatch: bool = False,
    layout_size_mismatch: bool = False,
    gameplay_chaos: bool = False,
    gameplay_wrap_chaos: bool = False,
    gameplay_correction: bool = False,
    gameplay_long_correction: bool = False,
    gameplay_disconnect: bool = False,
    gameplay_disconnect_correction: bool = False,
    gameplay_disconnect_receiving: bool = False,
    gameplay_disconnect_commit: bool = False,
    gameplay_disconnect_release: bool = False,
    gameplay_disconnect_release_ack: bool = False,
    input_sampling: bool = False,
    tick_failure: bool = False,
    prematch_skew: bool = False,
    via_relay: bool = False,
) -> None:
    chaos_case = gameplay_chaos or gameplay_wrap_chaos
    correction_case = gameplay_correction or gameplay_long_correction
    disconnect_case = (
        gameplay_disconnect
        or gameplay_disconnect_correction
        or gameplay_disconnect_receiving
        or gameplay_disconnect_commit
        or gameplay_disconnect_release
        or gameplay_disconnect_release_ack
    )
    host_port, join_port = reserve_port_pair()
    restart_requested = restart_join or restart_host_late
    restart_port = reserve_port() if restart_requested else 0
    while restart_requested and restart_port in (host_port, join_port):
        restart_port = reserve_port()
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    relay: UdpPairRelay | None = None
    peer_port_for_host = join_port
    peer_port_for_join = host_port
    if via_relay:
        relay_port = reserve_port()
        while relay_port in (host_port, join_port, restart_port):
            relay_port = reserve_port()
        relay = UdpPairRelay(relay_port, host_port, join_port)
        relay.start()
        peer_port_for_host = relay_port
        peer_port_for_join = relay_port
    host_child_env = child_env
    join_child_env = child_env
    trace_paths: list[Path] = []
    repro_dirs: list[Path] = []
    if chaos_case:
        host_fd, host_trace_name = tempfile.mkstemp(
            prefix="eggnoggplus_host_state_", suffix=".bin", dir=BUILD
        )
        join_fd, join_trace_name = tempfile.mkstemp(
            prefix="eggnoggplus_join_state_", suffix=".bin", dir=BUILD
        )
        os.close(host_fd)
        os.close(join_fd)
        trace_paths = [Path(host_trace_name), Path(join_trace_name)]
        host_child_env = child_env.copy()
        join_child_env = child_env.copy()
        host_child_env["EGGNOGGPLUS_TEST_STATE_TRACE"] = str(trace_paths[0])
        join_child_env["EGGNOGGPLUS_TEST_STATE_TRACE"] = str(trace_paths[1])
    if correction_case:
        repro_dirs = [
            Path(tempfile.mkdtemp(prefix="eggnoggplus_host_repro_", dir=BUILD)),
            Path(tempfile.mkdtemp(prefix="eggnoggplus_join_repro_", dir=BUILD)),
        ]
        if host_child_env is child_env:
            host_child_env = child_env.copy()
        if join_child_env is child_env:
            join_child_env = child_env.copy()
        host_child_env["EGGNOGGPLUS_TEST_REPRO_DIR"] = str(repro_dirs[0])
        join_child_env["EGGNOGGPLUS_TEST_REPRO_DIR"] = str(repro_dirs[1])
    if tick_failure:
        mode = "tickfailure"
    elif input_sampling:
        mode = "sampling"
    elif prematch_skew:
        mode = "skew"
    elif gameplay_disconnect_correction:
        mode = "disconnectcorrection"
    elif gameplay_disconnect_receiving:
        mode = "disconnectreceiving"
    elif gameplay_disconnect_commit:
        mode = "disconnectcommit"
    elif gameplay_disconnect_release:
        mode = "disconnectrelease"
    elif gameplay_disconnect_release_ack:
        mode = "disconnectreleaseack"
    elif gameplay_disconnect:
        mode = "disconnect"
    elif correction_case:
        mode = "longcorrection" if gameplay_long_correction else "correction"
    elif chaos_case:
        mode = "wrapchaos" if gameplay_wrap_chaos else "chaos"
    elif layout_size_mismatch:
        mode = "layoutsizemismatch"
    elif layout_mismatch:
        mode = "layoutmismatch"
    elif final_layout:
        mode = "layout"
    else:
        mode = "reject" if expect_rejection else "normal"
    host_args = [str(EXE), "host", str(host_port), str(peer_port_for_host), host_token, mode]
    if tamper_host:
        host_args.append("tamper")
    elif replay_host:
        host_args.append("replay")
    join_mode = ("layoutrestart" if final_layout else "restart") if restart_join else mode
    join_args = [str(EXE), join_role, str(join_port), str(peer_port_for_join), join_token, join_mode]
    if restart_join:
        host_args[5] = "layoutwatchrestart" if final_layout else "watchrestart"
        host_args.append(str(restart_port))
        join_args.append(str(restart_port))
    elif restart_host_late:
        host_args[5] = "restartlate"
        join_args[5] = "watchrestartlate"
        host_args.append(str(restart_port))
        join_args.append(str(restart_port))
    host = subprocess.Popen(
        host_args,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=creationflags,
        env=host_child_env,
    )
    join = subprocess.Popen(
        join_args,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=creationflags,
        env=join_child_env,
    )
    host_trace_data = b""
    join_trace_data = b""
    executor: ThreadPoolExecutor | None = None
    try:
        if gameplay_long_correction:
            pair_timeout = 60
        elif correction_case or chaos_case or disconnect_case:
            pair_timeout = 45
        else:
            pair_timeout = 30
        # Drain both children concurrently. Long seeded rollback sessions emit
        # enough diagnostic output to fill a Windows pipe; waiting for the host
        # to finish before reading the join pipe can block the join inside its
        # logger and manufacture a peer timeout.
        executor = ThreadPoolExecutor(max_workers=2)
        host_result = executor.submit(host.communicate, timeout=pair_timeout)
        join_result = executor.submit(join.communicate, timeout=pair_timeout)
        host_out, host_err = host_result.result()
        join_out, join_err = join_result.result()
    except BaseException as exc:
        host.kill()
        join.kill()
        host.wait()
        join.wait()
        if executor is not None:
            # A `with ThreadPoolExecutor` waits for communicate() before the
            # exception handler can kill its children. On Windows that turns a
            # useful bounded timeout into a permanent test-runner hang.
            executor.shutdown(wait=True, cancel_futures=True)
        child_details: list[str] = []
        for label, future in (("host", host_result), ("join", join_result)):
            try:
                child_out, child_err = future.result()
                child_details.append(
                    f"{label} return output:\n{child_out}\n{label} error:\n{child_err}"
                )
            except subprocess.TimeoutExpired as child_timeout:
                child_details.append(
                    f"{label} timed out; output:\n{child_timeout.output or ''}\n"
                    f"{label} error:\n{child_timeout.stderr or ''}"
                )
            except BaseException as child_exc:
                child_details.append(f"{label} communicate failed: {child_exc}")
        for trace_path in trace_paths:
            trace_path.unlink(missing_ok=True)
        for repro_dir in repro_dirs:
            shutil.rmtree(repro_dir, ignore_errors=True)
        relay_summary = relay.summary() if relay is not None else "direct"
        if relay is not None:
            relay.close()
        if isinstance(exc, subprocess.TimeoutExpired):
            partial_out = exc.output or ""
            partial_err = exc.stderr or ""
            raise AssertionError(
                f"{mode} pair timed out after {pair_timeout}s ({relay_summary})\n"
                f"partial output:\n{partial_out}\npartial error:\n{partial_err}\n" +
                "\n".join(child_details)
            ) from exc
        raise
    else:
        if executor is not None:
            executor.shutdown(wait=True)
        if relay is not None:
            relay.close()
    if chaos_case:
        try:
            host_trace_data = trace_paths[0].read_bytes()
            join_trace_data = trace_paths[1].read_bytes()
        finally:
            for trace_path in trace_paths:
                trace_path.unlink(missing_ok=True)

    repro_records: list[tuple[int, int, bytes, dict[str, str], str]] = []
    if correction_case and host.returncode == 0 and join.returncode == 0:
        try:
            for role, repro_dir in zip(("host", "join"), repro_dirs, strict=True):
                traces = list(repro_dir.glob("trace_*.bin"))
                metadata = list(repro_dir.glob("meta_*.txt"))
                if len(traces) != 1 or len(metadata) != 1:
                    raise AssertionError(
                        f"{role} did not preserve exactly one first-desync repro "
                        f"(traces={len(traces)}, metadata={len(metadata)})\n"
                        f"HOST OUT:\n{host_out}\nHOST ERR:\n{host_err}\n"
                        f"JOIN OUT:\n{join_out}\nJOIN ERR:\n{join_err}"
                    )
                trace_data = traces[0].read_bytes()
                if len(trace_data) < 13:
                    raise AssertionError(f"{role} desync trace is truncated")
                frame, state_len, checksum = struct.unpack_from(
                    "<III", trace_data, 0
                )
                if state_len == 0 or len(trace_data) != 12 + state_len:
                    raise AssertionError(
                        f"{role} desync trace has an invalid state length"
                    )
                meta_text = metadata[0].read_text(encoding="ascii")
                fields = dict(
                    line.split("=", 1)
                    for line in meta_text.splitlines()
                    if "=" in line
                )
                repro_records.append(
                    (frame, checksum, trace_data[12:], fields, traces[0].name)
                )
        finally:
            for repro_dir in repro_dirs:
                shutil.rmtree(repro_dir, ignore_errors=True)

    if host.returncode != 0 or join.returncode != 0:
        for repro_dir in repro_dirs:
            shutil.rmtree(repro_dir, ignore_errors=True)
        raise AssertionError(
            f"prematch pair failed ({host.returncode=}, {join.returncode=})\n"
            f"HOST OUT:\n{host_out}\nHOST ERR:\n{host_err}\n"
            f"JOIN OUT:\n{join_out}\nJOIN ERR:\n{join_err}"
        )
    if layout_mismatch or layout_size_mismatch:
        expected_marker = "LAYOUT REJECT PASS"
    else:
        expected_marker = "REJECT PASS" if expect_rejection else " PASS"
    if expected_marker not in host_out or expected_marker not in join_out:
        raise AssertionError(f"missing PASS output\n{host_out}\n{join_out}")
    if layout_mismatch or layout_size_mismatch:
        layout_pattern = re.compile(r"state_size=(\d+) layout_id=([0-9A-F]{8})")
        host_layout = layout_pattern.search(host_out)
        join_layout = layout_pattern.search(join_out)
        if not host_layout or not join_layout:
            raise AssertionError(f"missing rejected layout identities\n{host_out}\n{join_out}")
        host_size, host_layout_id = host_layout.groups()
        join_size, join_layout_id = join_layout.groups()
        if layout_size_mismatch:
            if host_size == join_size or host_layout_id != join_layout_id:
                raise AssertionError(
                    "capacity mismatch did not preserve a shared schema ID\n"
                    f"{host_out}\n{join_out}"
                )
        elif host_size != join_size or host_layout_id == join_layout_id:
            raise AssertionError(
                "schema mismatch did not preserve a shared state capacity\n"
                f"{host_out}\n{join_out}"
            )
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
        if "injected rollback load failure" not in join_err:
            raise AssertionError("the injected state-load failure was not exercised")
        if join_err.count("receiving host state") < 2:
            raise AssertionError("the failed state assembly was not retried")
    if tick_failure:
        marker = "TICK FAILURE PASS frame=0 restored=1"
        if marker not in host_out or marker not in join_out:
            raise AssertionError(
                "post-tick failure did not restore both peers atomically\n"
                f"{host_out}\n{join_out}"
            )
    if disconnect_case:
        expected_phase = (
            "release_ack"
            if gameplay_disconnect_release_ack
            else (
                "release"
                if gameplay_disconnect_release
                else (
                    "commit"
                    if gameplay_disconnect_commit
                    else (
                        "receiving"
                        if gameplay_disconnect_receiving
                        else (
                            "correction"
                            if gameplay_disconnect_correction
                            else "live"
                        )
                    )
                )
            )
        )
        marker = f"DISCONNECT PASS phase={expected_phase}"
        if marker not in host_out or marker not in join_out:
            raise AssertionError(
                f"missing {expected_phase} disconnect proof\n"
                f"{host_out}\n{join_out}"
            )
        notifying_output = (
            join_out if gameplay_disconnect_release_ack else host_out
        )
        detecting_output = (
            host_out if gameplay_disconnect_release_ack else join_out
        )
        if (
            "notified=1 active=0" not in notifying_output
            or "detected=1 active=0" not in detecting_output
        ):
            raise AssertionError(
                f"{expected_phase} disconnect was not bilateral and clean\n"
                f"{host_out}\n{join_out}"
            )
        if (
            gameplay_disconnect_correction
            or gameplay_disconnect_receiving
            or gameplay_disconnect_commit
            or gameplay_disconnect_release
            or gameplay_disconnect_release_ack
        ):
            if gameplay_disconnect_release_ack:
                join_barrier = re.search(
                    r"correction_phase=(\d+) peer_phase=(\d+) "
                    r"id=(\d+) snapshot=(\d+)",
                    join_out,
                )
                host_barrier = re.search(
                    r"correction_phase=(\d+) peer_phase=(\d+) "
                    r"id=(\d+) detected=1",
                    host_out,
                )
                if (
                    not host_barrier
                    or not join_barrier
                    or int(host_barrier.group(1)) != 7
                    or int(host_barrier.group(2)) != 8
                    or int(host_barrier.group(3)) == 0
                    or int(join_barrier.group(1)) != 8
                    or int(join_barrier.group(3)) == 0
                ):
                    raise AssertionError(
                        "release-ack disconnect lacked its bilateral barrier proof\n"
                        f"{host_out}\n{join_out}"
                    )
            else:
                host_barrier = re.search(
                    r"correction_phase=(\d+) peer_phase=(\d+) "
                    r"id=(\d+) snapshot=(\d+)",
                    host_out,
                )
                join_barrier = re.search(
                    r"correction_phase=(\d+) id=(\d+) detected=1",
                    join_out,
                )
                if (
                    not host_barrier
                    or not join_barrier
                    or int(host_barrier.group(1)) == 0
                    or int(host_barrier.group(3)) == 0
                    or int(join_barrier.group(1)) == 0
                ):
                    raise AssertionError(
                        "disconnect did not occur inside an identified correction barrier\n"
                        f"{host_out}\n{join_out}"
                    )
                expected_host_phase = (
                    7
                    if gameplay_disconnect_release
                    else (5 if gameplay_disconnect_commit else 2)
                )
                if int(host_barrier.group(1)) != expected_host_phase:
                    raise AssertionError(
                        f"{expected_phase} disconnect occurred in the wrong host phase\n"
                        f"{host_out}\n{join_out}"
                    )
                if (
                    gameplay_disconnect_receiving
                    and int(host_barrier.group(2)) != 3
                ):
                    raise AssertionError(
                        "receiving disconnect did not observe the peer in RECEIVING\n"
                        f"{host_out}\n{join_out}"
                    )
    if restart_join and "restarted=1" not in join_out:
        raise AssertionError(f"the join peer did not exercise prematch socket recovery\n{join_out}")
    if restart_host_late and (
        "restarted=1" not in host_out or "restarted=1" not in join_out
    ):
        raise AssertionError(
            "the post-state-sync host restart was not exercised on both peers\n"
            f"{host_out}\n{join_out}"
        )
    if final_layout:
        expected_host_layout = (
            "layout_finalized=1 layout_ready=1 "
            "bootstrap_size=1024 state_size=12288"
        )
        expected_join_layout = (
            "layout_finalized=1 layout_ready=1 "
            "bootstrap_size=2048 state_size=12288"
        )
        if expected_host_layout not in host_out or expected_join_layout not in join_out:
            raise AssertionError(
                "the finalized rollback layout was not used by both peers\n"
                f"{host_out}\n{join_out}"
            )
    if correction_case:
        host_repro, join_repro = repro_records
        host_frame, host_repro_checksum, host_state, host_meta, host_name = host_repro
        join_frame, join_repro_checksum, join_state, join_meta, join_name = join_repro
        host_remote_known = host_meta.get("remote_checksum_known") == "1"
        join_remote_known = join_meta.get("remote_checksum_known") == "1"
        if (
            host_frame != join_frame
            or host_repro_checksum == join_repro_checksum
            or host_state == join_state
            or host_meta.get("format") != "eggnoggplus-desync-repro-v1"
            or join_meta.get("format") != "eggnoggplus-desync-repro-v1"
            or host_meta.get("pair_tag") != join_meta.get("pair_tag")
            or host_meta.get("player") != "0"
            or join_meta.get("player") != "1"
            or host_meta.get("remote_fingerprint_known") != "1"
            or join_meta.get("remote_fingerprint_known") != "1"
            or host_meta.get("local_build_id")
                != join_meta.get("local_build_id")
            or host_meta.get("local_exe_id") != join_meta.get("local_exe_id")
            or host_meta.get("local_dll_id") != join_meta.get("local_dll_id")
            or host_meta.get("remote_build_id")
                != join_meta.get("local_build_id")
            or join_meta.get("remote_build_id")
                != host_meta.get("local_build_id")
            or host_meta.get("remote_exe_id") != join_meta.get("local_exe_id")
            or join_meta.get("remote_exe_id") != host_meta.get("local_exe_id")
            or host_meta.get("remote_dll_id") != join_meta.get("local_dll_id")
            or join_meta.get("remote_dll_id") != host_meta.get("local_dll_id")
            or host_meta.get("state_layout_id") in (None, "00000000")
            or host_meta.get("state_layout_id")
                != join_meta.get("state_layout_id")
            or host_meta.get("remote_state_layout_id")
                != join_meta.get("state_layout_id")
            or join_meta.get("remote_state_layout_id")
                != host_meta.get("state_layout_id")
            or host_meta.get("deterministic_config_id") in (None, "00000000")
            or host_meta.get("deterministic_config_id")
                != join_meta.get("deterministic_config_id")
            or int(host_meta.get("state_size", "0")) < len(host_state)
            or int(join_meta.get("state_size", "0")) < len(join_state)
            or not valid_chaos_metadata(host_meta)
            or not valid_chaos_metadata(join_meta)
            or int(host_meta.get("state_boundary_frame", "-1"))
                != ((host_frame + 1) & 0xFFFFFFFF)
            or int(join_meta.get("state_boundary_frame", "-1"))
                != ((join_frame + 1) & 0xFFFFFFFF)
            or int(host_meta.get("local_checksum", "-1"))
                != host_repro_checksum
            or int(join_meta.get("local_checksum", "-1"))
                != join_repro_checksum
            or host_meta.get("remote_checksum_known") not in ("0", "1")
            or join_meta.get("remote_checksum_known") not in ("0", "1")
            or not (host_remote_known or join_remote_known)
            or (
                host_remote_known
                and int(host_meta.get("remote_checksum", "-1"))
                    != join_repro_checksum
            )
            or (
                join_remote_known
                and int(join_meta.get("remote_checksum", "-1"))
                    != host_repro_checksum
            )
            or host_meta.get("pair_tag", "") not in host_name
            or join_meta.get("pair_tag", "") not in join_name
        ):
            raise AssertionError(
                "paired first-desync repro snapshots were incomplete or mismatched\n"
                f"host={host_name} {host_meta}\njoin={join_name} {join_meta}"
            )
        secret_text = "\n".join(
            (host_name, join_name, str(host_meta), str(join_meta))
        ).lower()
        if GOOD_TOKEN.lower() in secret_text or BAD_TOKEN.lower() in secret_text:
            raise AssertionError("desync repro metadata exposed a match token")
        correction_pattern = re.compile(
            r"CORRECTION PASS long=(\d+) target=(\d+) checksum=(\d+) frame=(\d+) "
            r"horizon=(\d+) checksum_rx_next=(\d+) checksum_peer_next=(\d+) "
            r"id=(\d+) snapshot=(\d+) resume=(\d+) transcript=(\d+) "
            r"injected=(\d+) sent=(\d+) received=(\d+) requests=(\d+) "
            r"rejected=(\d+) sim_drop=(\d+) sim_delay=(\d+) pred=(\d+) rb=(\d+)"
        )
        host_match = correction_pattern.search(host_out)
        join_match = correction_pattern.search(join_out)
        if not host_match or not join_match:
            raise AssertionError(
                f"missing coordinated-correction result\n{host_out}\n{join_out}"
            )
        host_values = tuple(int(value) for value in host_match.groups())
        join_values = tuple(int(value) for value in join_match.groups())
        (
            host_long,
            host_target,
            host_checksum,
            _,
            host_horizon,
            host_rx_next,
            host_peer_next,
            host_id,
            host_snapshot,
            host_resume,
            host_transcript,
            _,
            host_sent,
            _,
            host_requests,
            host_rejected,
            host_drops,
            host_delays,
            host_predictions,
            host_rollbacks,
        ) = host_values
        (
            join_long,
            join_target,
            join_checksum,
            _,
            join_horizon,
            join_rx_next,
            join_peer_next,
            join_id,
            join_snapshot,
            join_resume,
            join_transcript,
            join_injected,
            _,
            join_received,
            join_requests,
            join_rejected,
            join_drops,
            join_delays,
            join_predictions,
            join_rollbacks,
        ) = join_values
        expected_long = 1 if gameplay_long_correction else 0
        expected_target = 2047 if gameplay_long_correction else 179
        if (
            host_long != join_long
            or host_long != expected_long
            or host_target != join_target
            or host_target != expected_target
            or host_checksum != join_checksum
            or host_id != join_id
            or host_snapshot != join_snapshot
            or host_resume != join_resume
            or host_transcript != join_transcript
        ):
            raise AssertionError(
                "correction peers disagreed on the replay transcript identity\n"
                f"{host_out}\n{join_out}"
            )
        if join_injected != join_snapshot or not (
            join_snapshot < join_resume <= join_snapshot + 512
        ):
            raise AssertionError(
                f"correction did not use the injected divergence/exact bounded horizon\n{host_out}\n{join_out}"
            )
        if gameplay_long_correction and (
            join_injected < 600
            or min(
                host_predictions,
                join_predictions,
                host_rollbacks,
                join_rollbacks,
            )
            == 0
        ):
            raise AssertionError(
                "long correction did not inject after ring reuse with prediction "
                f"and rollback on both peers\n{host_out}\n{join_out}"
            )
        if min(host_horizon, join_horizon) < host_target or min(
            host_rx_next,
            host_peer_next,
            join_rx_next,
            join_peer_next,
        ) <= host_target:
            raise AssertionError(
                f"post-correction checksum proof did not drain both ways\n{host_out}\n{join_out}"
            )
        if host_sent == 0 or join_received == 0 or min(
            host_requests, join_requests
        ) == 0:
            raise AssertionError(
                f"REQUEST/OFFER correction path was not exercised\n{host_out}\n{join_out}"
            )
        if min(host_rejected, join_rejected) == 0 or min(
            host_drops, join_drops, host_delays, join_delays
        ) == 0:
            raise AssertionError(
                f"correction path did not exercise duplicate, loss, and delay\n{host_out}\n{join_out}"
            )
    if chaos_case:
        chaos_pattern = re.compile(
            r"CHAOS PASS start=(\d+) target=(\d+) wrapped=(\d+) "
            r"checksum=(\d+) frame=(\d+) "
            r"horizon=(\d+) checksum_rx_next=(\d+) "
            r"checksum_peer_next=(\d+) pred=(\d+) rb=(\d+) stalls=(\d+) "
            r"sim_drop=(\d+) sim_delay=(\d+)"
        )
        host_chaos = chaos_pattern.search(host_out)
        join_chaos = chaos_pattern.search(join_out)
        if not host_chaos or not join_chaos:
            raise AssertionError(
                f"missing gameplay-chaos result\n{host_out}\n{join_out}"
            )
        (
            host_start,
            host_target,
            host_wrapped,
            host_checksum,
            _,
            host_horizon,
            host_checksum_rx_next,
            host_checksum_peer_next,
            host_predictions,
            host_rollbacks,
            _,
            host_drops,
            host_delays,
        ) = (
            int(value) for value in host_chaos.groups()
        )
        (
            join_start,
            join_target,
            join_wrapped,
            join_checksum,
            _,
            join_horizon,
            join_checksum_rx_next,
            join_checksum_peer_next,
            join_predictions,
            join_rollbacks,
            _,
            join_drops,
            join_delays,
        ) = (
            int(value) for value in join_chaos.groups()
        )
        expected_start = (2**32 - 1) - 1023 if gameplay_wrap_chaos else 0
        expected_wrapped = 1 if gameplay_wrap_chaos else 0
        if (
            host_start != join_start
            or host_start != expected_start
            or host_target != join_target
            or host_wrapped != join_wrapped
            or host_wrapped != expected_wrapped
            or host_checksum != join_checksum
        ):
            raise AssertionError(
                "lossy gameplay peers disagreed at the confirmed target frame\n"
                f"{host_out}\n{join_out}"
            )

        def frame_after(left: int, right: int) -> bool:
            delta = (left - right) & 0xFFFFFFFF
            return delta != 0 and delta < 0x80000000

        def decode_exact_trace(data: bytes, role: str) -> list[tuple[int, bytes]]:
            records: list[tuple[int, bytes]] = []
            offset = 0
            while offset < len(data):
                if len(data) - offset < 12:
                    raise AssertionError(
                        f"{role} exact chaos trace has a truncated record header"
                    )
                frame, state_len, post_checksum = struct.unpack_from(
                    "<III", data, offset
                )
                offset += 12
                expected_frame = (expected_start + len(records)) & 0xFFFFFFFF
                if frame != expected_frame or state_len == 0:
                    raise AssertionError(
                        f"{role} exact chaos trace is noncanonical at frame {frame} "
                        f"(expected {expected_frame})"
                    )
                end = offset + state_len
                if end > len(data):
                    raise AssertionError(
                        f"{role} exact chaos trace truncates frame {frame}"
                    )
                records.append((post_checksum, data[offset:end]))
                offset = end
            return records

        host_trace = decode_exact_trace(host_trace_data, "host")
        join_trace = decode_exact_trace(join_trace_data, "join")
        expected_frames = 2048
        if len(host_trace) != expected_frames or len(join_trace) != expected_frames:
            raise AssertionError(
                "gameplay-chaos did not preserve every finalized frame "
                f"(expected={expected_frames}, host={len(host_trace)}, "
                f"join={len(join_trace)})"
            )
        for frame, (host_record, join_record) in enumerate(
            zip(host_trace, join_trace, strict=True)
        ):
            if host_record != join_record:
                serial_frame = (expected_start + frame) & 0xFFFFFFFF
                host_post, host_state = host_record
                join_post, join_state = join_record
                first_byte = next(
                    (
                        index
                        for index, (left, right) in enumerate(
                            zip(host_state, join_state, strict=False)
                        )
                        if left != right
                    ),
                    min(len(host_state), len(join_state)),
                )
                raise AssertionError(
                    "gameplay-chaos exact canonical history diverged at "
                    f"record={frame} frame={serial_frame} byte={first_byte} "
                    f"host_post={host_post} join_post={join_post} "
                    f"host_len={len(host_state)} join_len={len(join_state)}"
                )
        if host_trace[-1][0] != host_checksum:
            raise AssertionError(
                "gameplay-chaos target checksum did not match its exact trace record"
            )
        expected_target = (expected_start + expected_frames - 1) & 0xFFFFFFFF
        if host_target != expected_target or min(
            host_predictions,
            join_predictions,
            host_rollbacks,
            join_rollbacks,
        ) == 0:
            raise AssertionError(
                "gameplay-chaos did not cross multiple history generations "
                "with prediction and rollback on both peers\n"
                f"{host_out}\n{join_out}"
            )
        if (
            host_horizon != host_target
            and not frame_after(host_horizon, host_target)
        ) or (
            join_horizon != join_target
            and not frame_after(join_horizon, join_target)
        ):
            raise AssertionError(
                f"gameplay-chaos target was not mutually confirmed\n{host_out}\n{join_out}"
            )
        if not all(
            frame_after(value, host_target)
            for value in (
                host_checksum_rx_next,
                host_checksum_peer_next,
                join_checksum_rx_next,
                join_checksum_peer_next,
            )
        ):
            raise AssertionError(
                f"gameplay-chaos final checksum was not acknowledged both ways\n{host_out}\n{join_out}"
            )
        if host_drops == 0 or join_drops == 0 or host_delays == 0 or join_delays == 0:
            raise AssertionError(
                f"gameplay-chaos path did not exercise loss and delay\n{host_out}\n{join_out}"
            )
    if prematch_skew:
        skew_pattern = re.compile(
            r"SKEW PASS target=(\d+) checksum=(\d+) frame=(\d+) "
            r"horizon=(\d+) remote=(\d+) peer=(\d+) before=(\d+) "
            r"after=(\d+) delay=(\d+)"
        )
        host_skew = skew_pattern.search(host_out)
        join_skew = skew_pattern.search(join_out)
        if not host_skew or not join_skew:
            raise AssertionError(f"missing prematch-skew result\n{host_out}\n{join_out}")
        host_values = tuple(int(value) for value in host_skew.groups())
        join_values = tuple(int(value) for value in join_skew.groups())
        host_target, host_checksum, _, host_horizon, host_remote, host_peer, _, _, host_delay = host_values
        join_target, join_checksum, _, join_horizon, join_remote, join_peer, join_before, join_after, join_delay = join_values
        if host_target != join_target or host_checksum != join_checksum:
            raise AssertionError(
                f"prematch-skew peers disagreed at the confirmed target\n{host_out}\n{join_out}"
            )
        if host_delay != 4 or join_delay != 4 or join_before < 3 or join_after < join_before:
            raise AssertionError(
                f"prematch-skew input prefix was not preserved\n{host_out}\n{join_out}"
            )
        if min(host_horizon, host_remote, host_peer, join_horizon, join_remote, join_peer) < host_target:
            raise AssertionError(
                f"prematch-skew horizons did not pass the target\n{host_out}\n{join_out}"
            )
    if input_sampling:
        sampling_pattern = re.compile(
            r"SAMPLING PASS target=(\d+) checksum=(\d+) frame=(\d+) "
            r"horizon=(\d+) stall_frame=(\d+) input_frame=(\d+) "
            r"stale=(\d+) committed=(\d+) stalls=(\d+)"
        )
        host_sampling = sampling_pattern.search(host_out)
        join_sampling = sampling_pattern.search(join_out)
        if not host_sampling or not join_sampling:
            raise AssertionError(
                f"missing input-sampling result\n{host_out}\n{join_out}"
            )
        host_values = tuple(int(value) for value in host_sampling.groups())
        join_values = tuple(int(value) for value in join_sampling.groups())
        (
            host_target,
            host_checksum,
            _,
            host_horizon,
            host_stall_frame,
            host_input_frame,
            host_stale,
            host_committed,
            host_stalls,
        ) = host_values
        (
            join_target,
            join_checksum,
            _,
            join_horizon,
            _,
            _,
            join_stale,
            _,
            _,
        ) = join_values
        if host_target != join_target or host_checksum != join_checksum:
            raise AssertionError(
                "input-sampling peers disagreed at the confirmed target\n"
                f"{host_out}\n{join_out}"
            )
        if min(host_horizon, join_horizon) < host_target:
            raise AssertionError(
                f"input-sampling target was not mutually confirmed\n{host_out}\n{join_out}"
            )
        if (
            host_stall_frame == 0
            or host_input_frame != host_stall_frame + 1
            or host_stale != 0x40
            or join_stale != 0x40
            or host_committed != 0x80
            or host_stalls < 32
        ):
            raise AssertionError(
                "input-sampling commit boundary proof was incomplete\n"
                f"{host_out}\n{join_out}"
            )
    print(host_out.strip())
    print(join_out.strip())


def main() -> int:
    if os.name != "nt":
        print("prematch_net_test: skipped (Windows/WinSock test)")
        return 0
    try:
        child_env = build()
        receive_budget = subprocess.run(
            [str(EXE), "receive-budget"], cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=10, check=False, env=child_env,
        )
        if receive_budget.returncode != 0 or "receive-budget PASS" not in receive_budget.stdout:
            raise AssertionError(
                f"receive budget failed\nOUT:\n{receive_budget.stdout}\nERR:\n{receive_budget.stderr}"
            )
        print(receive_budget.stdout.strip())
        if "--receive-budget-only" in sys.argv[1:]:
            return 0
        if "--state-transaction-only" in sys.argv[1:]:
            state_transaction = subprocess.run(
                [str(EXE), "state-transaction"],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=10,
                check=False,
                env=child_env,
            )
            if (
                state_transaction.returncode != 0
                or "state-transaction PASS" not in state_transaction.stdout
            ):
                raise AssertionError(
                    "state transaction test failed\n"
                    f"OUT:\n{state_transaction.stdout}\nERR:\n{state_transaction.stderr}"
                )
            print(state_transaction.stdout.strip())
            print("prematch_net_test: state-transaction checks passed")
            return 0
        frame_ring = subprocess.run(
            [str(EXE), "frame-ring"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
            env=child_env,
        )
        if frame_ring.returncode != 0 or "frame-ring PASS" not in frame_ring.stdout:
            raise AssertionError(
                "frame-ring admission test failed\n"
                f"OUT:\n{frame_ring.stdout}\nERR:\n{frame_ring.stderr}"
            )
        print(frame_ring.stdout.strip())
        if "--frame-ring-only" in sys.argv[1:]:
            print("prematch_net_test: frame-ring checks passed")
            return 0
        if "--basic-only" in sys.argv[1:]:
            run_pair("join", expect_recovery=False, child_env=child_env)
            print("prematch_net_test: basic authenticated handshake checks passed")
            return 0
        if "--relay-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                via_relay=True,
            )
            print("prematch_net_test: symmetric-relay handshake checks passed")
            return 0
        if "--skew-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                prematch_skew=True,
            )
            print("prematch_net_test: prematch-skew checks passed")
            return 0
        if "--sampling-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                input_sampling=True,
            )
            print("prematch_net_test: input-sampling checks passed")
            return 0
        if "--tick-failure-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                tick_failure=True,
            )
            print("prematch_net_test: live-tick failure-atomicity checks passed")
            return 0
        if "--chaos-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_chaos=True,
            )
            print("prematch_net_test: gameplay-chaos checks passed")
            return 0
        if "--wrap-chaos-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_wrap_chaos=True,
            )
            print("prematch_net_test: uint32-wrap gameplay-chaos checks passed")
            return 0
        if "--correction-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_correction=True,
            )
            print("prematch_net_test: coordinated-correction checks passed")
            return 0
        if "--disconnect-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_disconnect=True,
            )
            print("prematch_net_test: live-disconnect checks passed")
            return 0
        if "--disconnect-correction-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_disconnect_correction=True,
            )
            print("prematch_net_test: correction-disconnect checks passed")
            return 0
        if "--disconnect-receiving-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_disconnect_receiving=True,
            )
            print("prematch_net_test: receiving-disconnect checks passed")
            return 0
        if "--disconnect-commit-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_disconnect_commit=True,
            )
            print("prematch_net_test: commit-disconnect checks passed")
            return 0
        if "--disconnect-release-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_disconnect_release=True,
            )
            print("prematch_net_test: release-disconnect checks passed")
            return 0
        if "--disconnect-release-ack-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_disconnect_release_ack=True,
            )
            print("prematch_net_test: release-ack disconnect checks passed")
            return 0
        if "--long-correction-only" in sys.argv[1:]:
            run_pair(
                "join",
                expect_recovery=False,
                child_env=child_env,
                gameplay_long_correction=True,
            )
            print("prematch_net_test: long coordinated-correction checks passed")
            return 0
        state_transaction = subprocess.run(
            [str(EXE), "state-transaction"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
            env=child_env,
        )
        if (
            state_transaction.returncode != 0
            or "state-transaction PASS" not in state_transaction.stdout
        ):
            raise AssertionError(
                "state transaction test failed\n"
                f"OUT:\n{state_transaction.stdout}\nERR:\n{state_transaction.stderr}"
            )
        print(state_transaction.stdout.strip())
        no_key = subprocess.run(
            [str(EXE), "nokey"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
            env=child_env,
        )
        if no_key.returncode != 0 or "nokey PASS" not in no_key.stdout:
            raise AssertionError(
                f"missing-key fail-closed test failed\nOUT:\n{no_key.stdout}\nERR:\n{no_key.stderr}"
            )
        print(no_key.stdout.strip())
        run_pair("join", expect_recovery=False, child_env=child_env)
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            via_relay=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            tick_failure=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            input_sampling=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            prematch_skew=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_chaos=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_wrap_chaos=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_correction=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_disconnect=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_disconnect_correction=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_disconnect_receiving=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_disconnect_commit=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_disconnect_release=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_disconnect_release_ack=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            gameplay_long_correction=True,
        )
        run_pair("join", expect_recovery=False, child_env=child_env, final_layout=True)
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            final_layout=True,
            restart_join=True,
        )
        run_pair("join", expect_recovery=False, child_env=child_env, layout_mismatch=True)
        run_pair("join", expect_recovery=False, child_env=child_env, layout_size_mismatch=True)
        run_pair("join", expect_recovery=False, child_env=child_env, restart_join=True)
        run_pair("join", expect_recovery=False, child_env=child_env, restart_host_late=True)
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            replay_host=True,
            require_join_rejects=True,
        )
        run_pair("joinfail", expect_recovery=True, child_env=child_env)
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
            join_token=BAD_TOKEN,
            expect_rejection=True,
            require_host_rejects=True,
            require_join_rejects=True,
        )
        run_pair(
            "join",
            expect_recovery=False,
            child_env=child_env,
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
