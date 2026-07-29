# Public source release

Do not change the visibility of the private development repository. Its history
contains runtime databases, server state, logs, generated binaries, local
configuration, and other files which do not belong in a public repository.
Deleting those files in a later commit does not remove them from Git history.

Use `tools/export_public_source.ps1` to create a flattened, sanitized source
tree with no inherited Git history:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/export_public_source.ps1 `
  -Destination ..\yule-src-public
```

The exporter:

- copies only tracked, explicitly allowed source/documentation trees;
- excludes game/runtime binaries, build products, caches, logs, crash/desync
  dumps, account/rating databases, the server secret, user configuration, and
  the retired cosmetics package;
- overlays public repository metadata and safe example configuration;
- scans exported text for private-key material and credential-like assignments;
- writes `SOURCE_SNAPSHOT.json` with the private source commit used as
  provenance, without copying its history.

By default, files come from `HEAD`, which prevents dirty runtime state from
entering a release. `-WorkingTree` is available for local review only and
should not be published until the intended source changes are committed and a
normal `HEAD` export produces the same result.

After reviewing the output, initialize a brand-new repository in the exported
directory and push that repository to a new public GitHub project. Do not add
the private repository as a remote and do not merge or graft its history.

```powershell
Set-Location ..\yule-src-public
git init -b main
git add .
git commit -m "Initial public source release"
git remote add origin https://github.com/loafiieee/Yule-src.git
git push -u origin main
```

Choose and add the intended project license before inviting third-party
contributions. Making source visible without a license does not grant reuse or
redistribution rights.
