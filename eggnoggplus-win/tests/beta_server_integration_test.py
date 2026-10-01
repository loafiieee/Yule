"""Exercise actual stable/beta servers with isolated data, never a live service."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from online_server_match_protocol_test import Client, reserve_port, SERVER, SERVER_DIR


def main():
    node = shutil.which("node")
    assert node, "Node is required"
    with tempfile.TemporaryDirectory(prefix="yule-beta-server-") as directory:
        temp = Path(directory)
        for channel in ("stable", "beta"):
            data = temp / channel
            data.mkdir()
            port = reserve_port()
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith(("EOS_", "TLS_", "ADMIN_", "DISCORD_", "LFG_"))}
            env.update(HOST="127.0.0.1", UDP_HOST="127.0.0.1", PORT=str(port),
                       UDP_PORT=str(port), RELEASE_CHANNEL=channel, ADMIN_ENABLED="0",
                       DB=str(data / "users.json"), RATINGS=str(data / "ratings.json"),
                       SECRET_FILE=str(data / "server_secret.key"))
            with (data / "server.log").open("w", encoding="utf-8") as log:
                process = subprocess.Popen([node, str(SERVER)], cwd=SERVER_DIR,
                                           env=env, stdout=log, stderr=subprocess.STDOUT)
                clients = []
                try:
                    deadline = time.monotonic() + 5
                    while True:
                        try:
                            client = Client(port)
                            clients.append(client)
                            break
                        except OSError:
                            assert process.poll() is None and time.monotonic() < deadline, "server startup failed"
                            time.sleep(.05)
                    client.send({"type": "server_info"})
                    assert client.receive_type("server_info")["release_channel"] == channel
                    wrong = "beta" if channel == "stable" else "stable"
                    for action in ("register", "login"):
                        client.send(dict(type=action, username="channeltest", password="fixture-only", release_channel=wrong))
                        assert "different release channel" in client.receive_type("auth_fail")["reason"]
                    if channel == "beta":
                        client.send(dict(type="register", username="channeltest", password="fixture-only"))
                        assert "different release channel" in client.receive_type("auth_fail")["reason"]
                    assert not (data / "users.json").exists() or "channeltest" not in json.loads((data / "users.json").read_text())["users"]
                    # Same account name can exist separately without either server
                    # authenticating an incompatible client or sharing its database.
                    client.send(dict(type="register", username="channeltest", password="fixture-only", release_channel=channel))
                    client.receive_type("auth_ok")
                    assert "channeltest" in json.loads((data / "users.json").read_text())["users"]
                finally:
                    for client in clients:
                        client.close()
                    process.terminate()
                    process.wait(timeout=5)
        for key in ("DB", "RATINGS", "SECRET_FILE"):
            bad = env.copy()
            bad.pop(key)
            result = subprocess.run([node, str(SERVER)], cwd=SERVER_DIR, env=bad,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)
            assert result.returncode != 0 and f"isolated {key} path" in result.stderr
    print("stable/beta server isolation and storage guards: OK")


if __name__ == "__main__":
    main()
