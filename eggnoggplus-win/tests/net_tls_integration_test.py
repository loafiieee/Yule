"""Guarded Schannel/Node TLS tests. Test roots stay in process memory only."""
from __future__ import annotations
import argparse
from contextlib import contextmanager
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
from prematch_net_test import ROOT, find_gcc, child_environment, assert_runtime_dlls

BUILD = ROOT / "build" / "net_tls_test"

def certificates(directory: Path, name: str, san: str, days: int = 1):
    openssl = Path(r"C:\msys64\mingw32\bin\openssl.exe")
    if not openssl.is_file():
        openssl = Path(shutil.which("openssl") or "openssl")
    def run(*args):
        subprocess.run([str(openssl), *map(str, args)], check=True, timeout=30,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    root = directory / "root.pem"
    if not root.exists():
        run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", directory / "root.key",
            "-out", root, "-days", "3", "-subj", "/CN=Yule temporary test CA",
            "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign")
        run("x509", "-in", root, "-outform", "DER", "-out", directory / "root.der")
    key, cert, csr, extensions = [directory / (name + suffix) for suffix in (".key", ".pem", ".csr", ".ext")]
    extensions.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
                          "extendedKeyUsage=serverAuth\nsubjectAltName=" + san + "\n", encoding="ascii")
    run("req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", csr, "-subj", "/CN=Yule test server")
    if days > 0:
        run("x509", "-req", "-in", csr, "-CA", root, "-CAkey", directory / "root.key", "-CAcreateserial",
            "-days", days, "-extfile", extensions, "-out", cert)
    else:
        (directory / "index").write_text("", encoding="ascii")
        (directory / "serial").write_text("1000\n", encoding="ascii")
        config = directory / "ca.cfg"
        config.write_text("[ca]\ndefault_ca=test\n[test]\n" +
            f"database={directory.as_posix()}/index\nserial={directory.as_posix()}/serial\n"
            f"new_certs_dir={directory.as_posix()}\ncertificate={root.as_posix()}\n"
            f"private_key={directory.as_posix()}/root.key\n" +
            "default_md=sha256\npolicy=policy\nx509_extensions=server\n[policy]\ncommonName=supplied\n[server]\n" +
            extensions.read_text(encoding="ascii"), encoding="ascii")
        run("ca", "-batch", "-config", config, "-in", csr, "-out", cert,
            "-startdate", "20250927000000Z", "-enddate", "20250928000000Z")
    return cert, key

@contextmanager
def fixture(cert: Path, key: Path):
    process = subprocess.Popen(["node", "tests/net_tls_server_fixture.js", str(cert), str(key)], cwd=ROOT,
                               text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        line = process.stdout.readline().strip()
        if not line.isdigit():
            raise RuntimeError("TLS server fixture failed to start")
        yield int(line), process
    finally:
        process.terminate()
        process.communicate(timeout=5)

class FragmentProxy:
    def __init__(self, target: int):
        self.socket = socket.socket()
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen(1)
        self.port = self.socket.getsockname()[1]
        self.target = target
        self.wire = bytearray()
        self.connections = []
        self.worker = threading.Thread(target=self.run, daemon=True)
        self.worker.start()

    def run(self):
        try:
            client, _ = self.socket.accept()
            server = socket.create_connection(("127.0.0.1", self.target))
            self.connections = [client, server]
            def pump(source, destination, fragment):
                offset = 0
                try:
                    while data := source.recv(4096):
                        if not fragment: self.wire.extend(data)
                        piece = 37 if fragment and offset < 4096 else 777
                        for start in range(0, len(data), piece):
                            destination.sendall(data[start:start + piece])
                            if fragment and offset < 4096: time.sleep(.001)
                        offset += len(data)
                except OSError:
                    pass
                finally:
                    try: destination.shutdown(socket.SHUT_WR)
                    except OSError: pass
            other = threading.Thread(target=pump, args=(server, client, True), daemon=True)
            other.start()
            pump(client, server, False)
            other.join(5)
        except OSError:
            pass

    def close(self):
        for connection in self.connections: connection.close()
        self.socket.close()
        self.worker.join(5)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--public-cert-check", action="store_true")
    args = parser.parse_args()
    gcc = find_gcc()
    assert_runtime_dlls(gcc)
    env = child_environment(gcc)
    BUILD.mkdir(parents=True, exist_ok=True)
    executable = BUILD / "net_tls_probe.exe"
    subprocess.run([gcc, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-D_WIN32_WINNT=0x0601", "-DNET_TLS_TEST", "tests/net_tls_probe.c", "net_tls.c", "online_control.c",
                    "-o", str(executable), "-lws2_32", "-liphlpapi", "-lsecur32", "-lcrypt32"],
                   cwd=ROOT, env=env, check=True, timeout=30)
    with tempfile.TemporaryDirectory(prefix="yule-tls-test-") as temporary:
        directory = Path(temporary)
        for name, san, days, mode, trusted in [
            ("valid", "IP:127.0.0.1", 1, "bulk", True),
            ("untrusted", "IP:127.0.0.1", 1, "reject", False),
            ("wrong-name", "DNS:example.invalid", 1, "reject", True),
            ("expired", "IP:127.0.0.1", -1, "reject", True),
        ]:
            cert, key = certificates(directory, name, san, days)
            with fixture(cert, key) as (port, process):
                proxy = FragmentProxy(port)
                try:
                    subprocess.run([str(executable), "127.0.0.1", str(proxy.port), mode,
                                    str(directory / "root.der") if trusted else "-"],
                                   cwd=ROOT, env=env, check=True, timeout=45)
                    assert b"PASSWORD" not in proxy.wire
                    if mode == "bulk":
                        assert bytes((i * 37 + 11) & 255 for i in range(512)) not in proxy.wire
                        assert process.stdout.readline().strip() == "PAYLOAD PASS"
                    print(name + ": PASS", flush=True)
                finally:
                    proxy.close()
    if args.public_cert_check:
        subprocess.run([str(executable), "auth.loafiieee.com", "443", "http", "-"],
                       cwd=ROOT, env=env, check=True, timeout=35)
    print("Schannel/Node TLS integration: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
