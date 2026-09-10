"""Verify our narrow CALL patch table against the actual native PE image."""
from pathlib import Path
import re
import struct
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "hooks.c").read_text(encoding="utf-8")
start = source.index("static int hooks_install_opponent_spawn_policy(void)")
end = source.index("// Old-style link filter", start)
body = source[start:end]
patches = re.findall(r"\{(0x[0-9A-F]+)u, \{([^}]+)\}, \(uintptr_t\)(\w+)\}", body)
assert len(patches) == 9, "All respawn, combat, and loser-removal gates required"
expected_targets = {
    0x427FAB: "hooks_opponent_respawn_bridge", 0x42B1A0: "hooks_opponent_transition_gate",
    **{address: "hooks_opponent_combat_gate" for address in
       (0x425974, 0x426894, 0x429C03, 0x429E3A, 0x429F0D, 0x42A9DF, 0x42DF4E)},
}
exe = (ROOT / "eggnoggplus.exe").read_bytes()
pe = struct.unpack_from("<I", exe, 0x3C)[0]
assert exe[pe:pe+4] == b"PE\0\0"
sections = struct.unpack_from("<H", exe, pe+6)[0]
optional_size = struct.unpack_from("<H", exe, pe+20)[0]
image_base = struct.unpack_from("<I", exe, pe+24+28)[0]
section_table = pe+24+optional_size

def native_bytes(address, length):
    rva = address-image_base
    for i in range(sections):
        section = section_table+i*40
        va, raw_size, raw = struct.unpack_from("<III", exe, section+12)
        if va <= rva and rva+length <= va+raw_size:
            return exe[raw+rva-va:raw+rva-va+length]
    raise AssertionError(f"Unmapped native address {address:x}")

for address_text, bytes_text, target in patches:
    address = int(address_text, 16)
    expected = bytes(int(value, 16) for value in bytes_text.split(","))
    assert expected_targets.pop(address) == target
    assert native_bytes(address, 5) == expected, hex(address)
    assert expected[0] == 0xE8
    assert address+5+struct.unpack("<i", expected[1:])[0] == (0x41F5E0 if address == 0x42A9DF else 0x41ED60)
assert not expected_targets
assert body.index("memcmp") < body.index("VirtualProtect") < body.index("memcpy")
assert "patches[i].expected, 5" in body[body.index("if (!FlushInstructionCache"):]
assert "custom_maps_opponent_spawn_policy(*g_hook_map_selector" in source
assert '"push %ebx\\n\\t"' in source and '"call _hooks_opponent_respawn_target\\n\\t"' in source
print("PASS: nine opponent policy call sites match native executable; all-or-nothing patch preflight")

# This wrapper is only a boolean-normalizing win-condition call, not a goal action.
assert native_bytes(0x41F5E0, 14) == bytes.fromhex('E8 7B F7 FF FF 85 C0 0F 95 C0 0F B6 C0 C3')
# A blocked death timer skips the decrement; overriding it must reach that decrement.
assert native_bytes(0x42A9E4, 8) == bytes.fromhex('85 C0 0F 84 45 03 00 00')
assert native_bytes(0x42AD31, 4) == bytes.fromhex('83 6B 18 01')
