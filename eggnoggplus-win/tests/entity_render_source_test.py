"""Read-only native ABI evidence for managed entity draw insertion."""
from pathlib import Path
import struct
root = Path(__file__).resolve().parents[1]
exe = (root / "eggnoggplus.exe").read_bytes()
pe = struct.unpack_from("<I", exe, 0x3c)[0]
base = struct.unpack_from("<I", exe, pe+52)[0]
table = pe+24+struct.unpack_from("<H", exe, pe+20)[0]
def read(address, count):
    for i in range(struct.unpack_from("<H", exe, pe+6)[0]):
        va, size, raw = struct.unpack_from("<III", exe, table+40*i+12)
        offset = address-base-va
        if 0 <= offset and offset+count <= size:
            return exe[raw+offset:raw+offset+count]
    raise AssertionError(hex(address))
assert read(0x41c3f0,7) == bytes.fromhex("55 b9 18 00 00 00 57")
# Native layer arrives in EAX; final draw call selects -2.
assert read(0x41c3f7,2) == bytes.fromhex("89 c2")
assert read(0x423140,5) == bytes.fromhex("b8 fe ff ff ff")
call = read(0x423145,5)
assert call[0] == 0xe8 and 0x42314a+struct.unpack("<i",call[1:])[0] == 0x41c3f0
# Native sprite positions subtract camera X and invert world Y relative to camera.
assert read(0x41c443,6) == bytes.fromhex("d8 25 64 a3 55 00")
assert read(0x41c44d,2) == bytes.fromhex("d9 e0")
assert read(0x41c456,6) == bytes.fromhex("d8 25 60 a3 55 00")
source=(root/"hooks.c").read_text(encoding="utf-8")
assert "regparm(1)" in source
hook = source[source.index("static void __attribute__((regparm(1))) hooked_entity_draw_things"):]
assert hook.index("if(native_layer==1)draw_custom_entities_for_order(0)") < hook.index("p_entity_draw_things_trampoline(native_layer)") < hook.index("if(native_layer==-2)draw_custom_entities_for_order(1)")
assert "if(view.visual.layer!=draw_order)continue;" in source
assert "map_script_entity_render_next(&cursor,&view)" in source
print("entity render native ABI and insertion checks passed")
