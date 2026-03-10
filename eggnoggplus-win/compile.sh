gcc -m32 -shared -o SDL2.dll dllmain.c stubs.c hooks.c custom_maps.c lua_manager.c font_ext.c texture_ext.c log.c -lkernel32 -luser32 -lopengl32 -lluajit-5.1 -I/mingw32/include
