# Repository-wide updater

`tools/update_repository.sh` safely fast-forwards a deployed Yule Git checkout
without touching its live online server. It is the complement of
`online_server/update_server.sh`:

- `tools/update_repository.sh` updates the repository except protected live
  state.
- `online_server/update_server.sh` validates, replaces, restarts, and probes
  only the online server.

The repository updater always preserves:

- the entire `online_server/` directory;
- runtime configuration, logs, crash/desync output, mod storage, and caches;
- `build/`, `dist/`, IDE settings, and untracked community files;
- any extra repository-relative paths named in `YULE_PRESERVE_PATHS`.

It refuses non-protected tracked edits or staged changes instead of silently
discarding development work. A fresh clone is validated before deployment.
Only fast-forward updates are accepted. Files removed upstream are pruned only
when they were tracked locally, and an interrupted apply automatically restores
the previous files, Git index, branch, and upstream ref.

## Use

Run it from anywhere inside the checkout:

```bash
cd ~/Yule
bash ./eggnoggplus-win/tools/update_repository.sh --dry-run
bash ./eggnoggplus-win/tools/update_repository.sh
```

The normal source is the checkout's `origin`, branch `main`. Override those
when needed:

```bash
YULE_REPOSITORY_URL=https://github.com/loafiieee/Yule.git \
YULE_REPOSITORY_REF=main \
bash ./eggnoggplus-win/tools/update_repository.sh
```

Preserve additional paths with a colon-separated list. Paths are relative to
the Git repository root:

```bash
YULE_PRESERVE_PATHS='notes:eggnoggplus-win/maps/local_pack' \
bash ./eggnoggplus-win/tools/update_repository.sh
```

The live service keeps running because no file beneath `online_server/` is
opened, replaced, or removed. Deploy online-server changes separately:

```bash
cd ~/Yule/eggnoggplus-win/online_server
./update_server.sh
```

`YULE_SKIP_VALIDATION=1` skips optional Node and Python source checks. It does
not disable structural validation, fast-forward enforcement, protected-path
checks, conflict checks, post-copy verification, or rollback.
