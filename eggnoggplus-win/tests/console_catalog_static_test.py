import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HOOKS = (ROOT / "hooks.c").read_text(encoding="utf-8")
CATALOG = (ROOT / "console_catalog.c").read_text(encoding="utf-8")

catalog_block = re.search(
    r"static const char\* const k_commands\[\] = \{(.*?)\n\};",
    CATALOG,
    re.DOTALL,
)
assert catalog_block, "console command catalog array was not found"
known = set(re.findall(r'"([a-z0-9_.]+)"', catalog_block.group(1)))

# Commands are currently dispatched by the hook orchestrator. Any literal
# command accepted there must be represented in the catalog used by completion.
dispatched = set(re.findall(r'_stricmp\(cmd, "([a-z0-9_.]+)"\)', HOOKS))
missing = sorted(dispatched - known)
assert not missing, (
    "console commands execute but are absent from autocomplete/catalog: "
    f"{missing}"
)

for command in ("ggpo.roundtrip", "ggpo.selftest", "ggpo.local"):
    assert command in known, f"developer command missing from catalog: {command}"

print(
    f"console catalog coverage: OK ({len(dispatched)} dispatched, "
    f"{len(known)} catalog entries)"
)
