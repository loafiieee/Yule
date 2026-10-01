"""Guarded offline transport regression tests; no game or account is launched."""
from pathlib import Path
import subprocess
from prematch_net_test import ROOT, find_gcc, child_environment, assert_runtime_dlls

def main():
    gcc = find_gcc()
    assert_runtime_dlls(gcc)
    env = child_environment(gcc)
    output = ROOT / "build" / "eos_net_regression"
    output.mkdir(parents=True, exist_ok=True)
    include = Path(r"C:\Users\potato\Downloads\eos-sdk-1.18.1.2-minimal\Include")
    if not (include / "eos_p2p.h").is_file():
        raise RuntimeError("EOS C SDK headers are required")
    common = [gcc, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-D_WIN32_WINNT=0x0601"]
    eos = output / "eos_transport_lifecycle_test.exe"
    subprocess.run(common + ["-DYULE_ENABLE_EOS", "-isystem", str(include),
        "tests/eos_transport_lifecycle_test.c", "ggpo_transport_native.c", "-lws2_32", "-o", str(eos)],
        cwd=ROOT, env=env, check=True, timeout=30)
    subprocess.run([str(eos)], cwd=ROOT, env=env, check=True, timeout=10)
    tcp = output / "net_ext_test.exe"
    subprocess.run(common + ["tests/net_ext_test.c", "net_ext.c", "net_tls.c", "online_control.c",
        "-lws2_32", "-liphlpapi", "-lsecur32", "-o", str(tcp)], cwd=ROOT, env=env, check=True, timeout=30)
    subprocess.run([str(tcp)], cwd=ROOT, env=env, check=True, timeout=20)
    subprocess.run(["python", "tests/gameplay_service_test.py"], cwd=ROOT, env=env, check=True, timeout=30)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
