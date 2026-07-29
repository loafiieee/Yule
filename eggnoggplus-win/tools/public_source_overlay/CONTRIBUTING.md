# Contributing

The public API and rollback format are still being stabilized. Before changing
an exported API, map schema, serialized state field, control protocol, match
protocol, or P2P wire structure, update its specification and add focused
compatibility tests.

Keep pull requests narrow and include:

- the behavior or bug being addressed;
- user-visible and compatibility impact;
- tests run through the guarded repository runners;
- documentation changes for public behavior;
- confirmation that no game binaries, generated output, runtime state, user
  data, credentials, tokens, or deployment secrets were added.

Do not run native test executables from `build/` directly and do not automate
launching the game. Use the runners documented in `README.md` and `TESTING.md`.

Security issues and suspected credential exposure should not be opened as
public issues; use the private contact process in `SECURITY.md`.
