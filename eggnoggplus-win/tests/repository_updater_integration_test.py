import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
UPDATER = ROOT / "tools" / "update_repository.sh"


def run(command, cwd, *, check=True, env=None):
    return subprocess.run(
        command,
        cwd=cwd,
        check=check,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def git(cwd, *args):
    return run(["git", *args], cwd).stdout.strip()


def write(root, relative, contents):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def shell_path(path, bash):
    if os.name != "nt":
        return str(path)
    result = run(
        [bash, "-lc", f"cygpath -u {str(path)!r}"],
        ROOT,
    )
    return result.stdout.strip()


bash = shutil.which("bash")
if not bash:
    print("repository updater integration: SKIP (bash unavailable)")
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix="yule-repository-updater-") as temporary:
    temp = Path(temporary)
    upstream = temp / "upstream"
    target = temp / "target"
    project = Path("eggnoggplus-win")

    upstream.mkdir()
    git(upstream, "init", "-b", "main")
    git(upstream, "config", "user.name", "Yule updater test")
    git(upstream, "config", "user.email", "updater-test@example.invalid")

    write(upstream, "README.md", "root v1\n")
    write(upstream, ".vscode/settings.json", '{"version": 1}\n')
    write(upstream, project / "hooks.c", "framework v1\n")
    write(upstream, project / "compile.sh", "#!/usr/bin/env bash\n")
    write(upstream, project / "tools/marker.txt", "tools\n")
    write(upstream, project / "tests/marker.txt", "tests\n")
    write(upstream, project / "mods/example/config.cfg", "setting=upstream-v1\n")
    write(upstream, project / "docs-site/check-docs.js", "process.exit(0);\n")
    write(upstream, project / "old.txt", "remove me\n")
    write(upstream, "notes/preserved.txt", "preserved v1\n")
    write(upstream, project / "online_server/server.js", "server v1\n")
    write(
        upstream,
        project / "online_server/package.json",
        '{"scripts":{"check":"node --check server.js","test":"node --test"}}\n',
    )
    git(upstream, "add", ".")
    git(upstream, "commit", "-m", "v1")

    git(temp, "clone", str(upstream), str(target))
    write(target, project / "online_server/server.js", "live server\n")
    write(target, project / "mods/example/config.cfg", "setting=live\n")
    write(target, project / "online_server/users.json", '{"live":true}\n')
    write(target, project / "maps/local.map", "untracked map\n")
    write(target, "operator-notes.txt", "untracked root file\n")

    write(upstream, "README.md", "root v2\n")
    write(upstream, ".vscode/settings.json", '{"version": 2}\n')
    write(upstream, project / "hooks.c", "framework v2\n")
    write(upstream, project / "new.txt", "new tracked file\n")
    bootstrap_contents = "#!/usr/bin/env bash\nprintf 'bootstrap updater\\n'\n"
    write(
        upstream,
        project / "tools/update_repository.sh",
        bootstrap_contents,
    )
    write(
        target,
        project / "tools/update_repository.sh",
        bootstrap_contents,
    )
    write(upstream, project / "mods/example/config.cfg", "setting=upstream-v2\n")
    write(upstream, project / "online_server/server.js", "server v2\n")
    (upstream / project / "old.txt").unlink()
    git(upstream, "add", "-A")
    git(upstream, "commit", "-m", "v2")
    v2 = git(upstream, "rev-parse", "HEAD")

    environment = os.environ.copy()
    environment.update(
        {
            "YULE_REPOSITORY_URL": shell_path(upstream, bash),
            "YULE_REPOSITORY_REF": "main",
            "YULE_TARGET_ROOT": shell_path(target, bash),
            "YULE_PROJECT_PATH": project.as_posix(),
            "YULE_SKIP_VALIDATION": "1",
        }
    )
    updater_path = shell_path(UPDATER, bash)
    result = run([bash, updater_path], ROOT, env=environment)
    assert "repository update complete" in result.stdout
    assert "adopting byte-identical untracked upstream file" in result.stdout
    assert git(target, "rev-parse", "HEAD") == v2
    assert (target / "README.md").read_text(encoding="utf-8") == "root v2\n"
    assert (target / project / "hooks.c").read_text(encoding="utf-8") == (
        "framework v2\n"
    )
    assert (target / project / "new.txt").is_file()
    assert (target / project / "tools/update_repository.sh").read_text(
        encoding="utf-8"
    ) == bootstrap_contents
    assert not (target / project / "old.txt").exists()

    # Every live/local path survives even though the branch and index advance.
    assert (target / project / "online_server/server.js").read_text(
        encoding="utf-8"
    ) == "live server\n"
    assert (target / project / "online_server/users.json").is_file()
    assert (target / project / "mods/example/config.cfg").read_text(
        encoding="utf-8"
    ) == "setting=live\n"
    assert (target / project / "maps/local.map").is_file()
    assert (target / "operator-notes.txt").is_file()
    assert (target / ".vscode/settings.json").read_text(
        encoding="utf-8"
    ) == '{"version": 1}\n'
    assert not git(target, "diff", "--cached", "--name-only")

    write(upstream, project / "hooks.c", "framework v3\n")
    write(upstream, "notes/preserved.txt", "preserved v3\n")
    git(upstream, "add", ".")
    git(upstream, "commit", "-m", "v3")
    v3 = git(upstream, "rev-parse", "HEAD")

    dry_run = run([bash, updater_path, "--dry-run"], ROOT, env=environment)
    assert "dry run complete" in dry_run.stdout
    assert git(target, "rev-parse", "HEAD") == v2
    assert (target / project / "hooks.c").read_text(
        encoding="utf-8"
    ) == "framework v2\n"

    # A non-runtime tracked edit fails closed before any apply begins.
    write(target, project / "hooks.c", "local edit\n")
    refused = run([bash, updater_path], ROOT, check=False, env=environment)
    assert refused.returncode != 0
    assert "non-runtime local edits" in refused.stdout
    assert git(target, "rev-parse", "HEAD") == v2
    assert (target / project / "hooks.c").read_text(
        encoding="utf-8"
    ) == "local edit\n"

    write(target, project / "hooks.c", "framework v2\n")
    environment["YULE_PRESERVE_PATHS"] = "notes"
    updated = run([bash, updater_path], ROOT, env=environment)
    assert "repository update complete" in updated.stdout
    assert git(target, "rev-parse", "HEAD") == v3
    assert (target / project / "hooks.c").read_text(
        encoding="utf-8"
    ) == "framework v3\n"
    assert (target / "notes/preserved.txt").read_text(
        encoding="utf-8"
    ) == "preserved v1\n"
    assert (target / project / "online_server/server.js").read_text(
        encoding="utf-8"
    ) == "live server\n"

    # A genuinely different untracked file remains a hard conflict.
    write(upstream, project / "collision.txt", "upstream contents\n")
    git(upstream, "add", ".")
    git(upstream, "commit", "-m", "v4")
    write(target, project / "collision.txt", "local contents\n")
    collision = run([bash, updater_path], ROOT, check=False, env=environment)
    assert collision.returncode != 0
    assert "upstream file conflicts with untracked local path" in collision.stdout
    assert git(target, "rev-parse", "HEAD") == v3
    assert (target / project / "collision.txt").read_text(
        encoding="utf-8"
    ) == "local contents\n"

print("repository-wide updater integration: OK")
