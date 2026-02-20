SKIP = {"SDL_GL_SwapWindow", "SDL_PollEvent"}

with open("SDL2.def", "r") as f:
    lines = f.readlines()

funcs = []
for line in lines:
    name = line.split(";")[0].strip()
    if not name.startswith("SDL_"):
        continue
    if name in SKIP:
        continue
    funcs.append(name)

out = []
out.append('#include <windows.h>\n')
out.append('extern HMODULE real_sdl;\n\n')

for fn in funcs:
    out.append(f'void* p_{fn} = NULL;\n')

out.append('\nvoid init_stubs() {\n')
for fn in funcs:
    out.append(f'    p_{fn} = GetProcAddress(real_sdl, "{fn}");\n')
out.append('}\n\n')

for fn in funcs:
    out.append(f'__attribute__((naked)) void {fn}() {{ asm("jmp *%0" : : "m"(p_{fn})); }}\n')

with open("stubs.c", "w") as f:
    f.writelines(out)

print("Done!")