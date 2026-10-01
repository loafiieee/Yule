"""Prepare an isolated v17 source tree with only the channel switcher backport.

The stable transport, packet layout, rollback and online login stay at the
specified Git revision. No Git checkout or installed game is modified here.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ref", default="d36b073")
    parser.add_argument("--version", default="1.932")
    args = parser.parse_args()
    assert re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", args.version)
    prefix = git("rev-parse", "--show-prefix").decode().strip()
    target = ROOT / "build" / "private_beta" / "channel_bridge_source"
    if target.exists():
        raise RuntimeError("Bridge tree already exists; preserve it before preparing another build.")
    tracked = git("ls-tree", "-r", "--full-tree", "--name-only", args.ref).decode().splitlines()
    paths = []
    for path in tracked:
        if not path.startswith(prefix):
            continue
        rel = path.removeprefix(prefix)
        if "/" not in rel and (Path(rel).suffix in {".c", ".h", ".def"} or rel == "compile.sh"):
            paths.append(path)
        elif rel.startswith("third_party/"):
            paths.append(path)
    if not paths:
        raise RuntimeError("No stable source paths found; refusing a whole-repository export")
    archive_root = ROOT.parents[len(Path(prefix).parts) - 1] if prefix else ROOT
    archive = subprocess.check_output(["git", "-C", str(archive_root), "archive",
                                       "--format=zip", args.ref, "--", *paths])
    target.mkdir(parents=True)
    with zipfile.ZipFile(io.BytesIO(archive)) as package:
        for item in package.infolist():
            if item.is_dir():
                continue
            rel = item.filename.removeprefix(prefix)
            if rel.startswith("../") or Path(rel).is_absolute():
                raise RuntimeError("Unexpected archive path")
            dest = target / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(package.read(item))
    # The updater is provider-independent. Copy only its controls and tooling.
    for name in ["update_ext.c", "update_ext.h", "update_channels.inc", "updater_helper.c",
                 "console_catalog.c", "lua_manager.c"]:
        shutil.copy2(ROOT / name, target / name)
    header = (target / "update_ext.h").read_text()
    header = re.sub(r'#define FRAMEWORK_VERSION "[^"]+"',
                    '#define FRAMEWORK_VERSION "' + args.version + '"', header)
    (target / "update_ext.h").write_text(header)

    base_hooks = (target / "hooks.c").read_text()
    diff = git("diff", args.ref, "--unified=3", "--", "hooks.c").decode()
    count = 0
    for hunk in re.split(r"(?=^@@ )", diff, flags=re.M)[1:]:
        if not any(key in hunk for key in ["update_ext_channel", "update_ext_switch", "update.channel",
                                           "FW_SETTING_BETA_CHANNEL", "online_hub_channel", "release_channel"]):
            continue
        if '"eos_connect_token"' in hunk:
            continue  # This mixed hunk is handled below without EOS login code.
        old, new = [], []
        for line in hunk.splitlines()[1:]:
            if line.startswith((" ", "-")):
                old.append(line[1:])
            if line.startswith((" ", "+")):
                new.append(line[1:])
        before, after = "\n".join(old), "\n".join(new)
        if base_hooks.count(before) != 1:
            raise RuntimeError("Bridge hunk no longer matches stable source uniquely")
        base_hooks = base_hooks.replace(before, after, 1)
        count += 1
    base_hooks = base_hooks.replace(
        '    return update_ext_channel_server(g_online_cfg.server_host,\n'
        '        sizeof(g_online_cfg.server_host), &g_online_cfg.server_port, &g_online_cfg.server_tls);',
        '    int tls = 0;\n'
        '    int result = update_ext_channel_server(g_online_cfg.server_host,\n'
        '        sizeof(g_online_cfg.server_host), &g_online_cfg.server_port, &tls);\n'
        '    /* This bridge never signs in to a v18/TLS beta endpoint. */\n'
        '    if (result > 0 && (tls || strcmp(update_ext_channel(), "stable") != 0)) return -1;\n'
        '    return result;')
    anchor = '    } else if (_stricmp(type, "server_info") == 0) {\n        if (!g_online_server_info_pending || !g_online_auth_pending) return;'
    assert base_hooks.count(anchor) == 1
    base_hooks = base_hooks.replace(anchor, anchor + '\n'
        '        {\n'
        '            char channel[16] = "stable";\n'
        '            if (online_control_json_get_string(line, "release_channel", channel,\n'
        '                    sizeof(channel)) != ONLINE_CONTROL_JSON_OK) text_copy(channel, sizeof(channel), "stable");\n'
        '            if (strcmp(channel, update_ext_channel()) != 0) {\n'
        '                online_server_disconnect("This server belongs to a different release channel.");\n'
        '                return;\n'
        '            }\n'
        '        }', 1)
    assert "yule_eos_" not in base_hooks and '"eos_runtime.h"' not in base_hooks
    (target / "hooks.c").write_text(base_hooks)
    # Use the current standalone/static helper; keep the original DLL sources.
    compile_source = (ROOT / "compile.sh").read_text()
    for addition in [" ggpo_transport_native.c", "  eos_runtime.c ggpo_transport_eos.c\n", " ui_text.c", " net_tls.c"]:
        compile_source = compile_source.replace(addition, "")
    compile_source = re.sub(r"eos_flags=\(\).*?\nfi\n", "eos_flags=()\n", compile_source, count=1, flags=re.S)
    compile_source = re.sub(r'if \[\[ -n "\$\{EOS_SDK_DIR:-\}" \]\]; then\n  cp -f.*?\nfi\n', "", compile_source, flags=re.S)
    compile_source = "\n".join(line for line in compile_source.splitlines()
                               if not line.startswith("cp -f third_party/tandy2k/")) + "\n"
    assert "-DYULE_ENABLE_EOS" not in compile_source
    (target / "compile.sh").write_text(compile_source)
    for name in ["lua51.dll", "libgcc_s_dw2-1.dll", "libwinpthread-1.dll", "SDL2_mixer.dll"]:
        shutil.copy2(ROOT / name, target / name)
    (target / "tools").mkdir(exist_ok=True)
    for name in ["build_release.ps1", "create_linux_installer_zip.py"]:
        shutil.copy2(ROOT / "tools" / name, target / "tools" / name)
    shutil.copytree(ROOT / "dist" / "installer", target / "dist" / "installer")
    # Explicitly establish that the v17 network implementation is byte-identical.
    hashes = {}
    for name in ["ggpo_net.c", "ggpo_net.h", "net_ext.c", "net_ext.h", "online_control.c", "online_control.h"]:
        original = git("show", args.ref + ":" + prefix + name)
        exported = (target / name).read_bytes()
        assert original == exported, "Stable network source changed: " + name
        hashes[name] = hashlib.sha256(exported).hexdigest()
    report = {"source_commit": git("rev-parse", args.ref).decode().strip(),
              "version": args.version, "p2p_protocol": 17, "eos": False,
              "channel_hunks": count, "unchanged_network_sha256": hashes}
    (target / "bridge_provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Prepared channel-only v17 bridge: " + str(target))
    print("Network sources match stable revision; no EOS integration included.")


if __name__ == "__main__":
    main()
