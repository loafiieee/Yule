#include <windows.h>
#include <float.h>
#include <stdint.h>
#include <string.h>

#include "cursor_ext.h"

/* eggnoggplus.exe fixed-address contract, verified against the checked-in
 * executable/Ghidra export. cursor_draw has three cdecl float arguments plus
 * two hidden register inputs: EAX selects shadow/color and EDX selects mouse or
 * player cursor. An ordinary C function-pointer call is therefore unsafe. */
#define ADDR_CURSOR_DRAW       0x42FEB0u
#define ADDR_GLOBAL_SCALE      0x448384u
#define ADDR_MOUSE_TIMEOUT     0x547B64u
#define ADDR_TURTLE_RESET      0x4092D0u
#define ADDR_TURTLE_SET_SCALE  0x409080u
#define ADDR_TURTLE_HOME       0x448060u
#define ADDR_TURTLE_STATE      0x4480C0u
#define TURTLE_STATE_SIZE      0x60u

typedef void (__cdecl *cursor_ext_void_fn)(void);
typedef void (__cdecl *cursor_ext_scale_fn)(double);

#if defined(__GNUC__) && defined(__i386__)
/* Stack on entry: return, x, y, spin, shadow. Each push from 12(%esp) is
 * deliberate: after the preceding push it addresses the next original
 * argument, producing native cdecl order spin/y/x. */
static void __attribute__((naked, noinline))
cursor_ext_native_mouse_pass(float x __attribute__((unused)),
                             float y __attribute__((unused)),
                             float spin __attribute__((unused)),
                             int shadow __attribute__((unused))) {
    __asm__ __volatile__(
        "movl 16(%esp), %eax\n\t"
        "xorl %edx, %edx\n\t"
        "pushl 12(%esp)\n\t"
        "pushl 12(%esp)\n\t"
        "pushl 12(%esp)\n\t"
        "movl $0x42FEB0, %ecx\n\t"
        "call *%ecx\n\t"
        "addl $12, %esp\n\t"
        "ret\n\t"
    );
}
#endif

int cursor_ext_draw_vanilla_mouse(float x, float y) {
    volatile const float* global_scale =
        (volatile const float*)(uintptr_t)ADDR_GLOBAL_SCALE;
    volatile int* mouse_timeout =
        (volatile int*)(uintptr_t)ADDR_MOUSE_TIMEOUT;
    cursor_ext_void_fn turtle_reset =
        (cursor_ext_void_fn)(uintptr_t)ADDR_TURTLE_RESET;
    cursor_ext_scale_fn turtle_set_scale =
        (cursor_ext_scale_fn)(uintptr_t)ADDR_TURTLE_SET_SCALE;
    unsigned char* turtle_home =
        (unsigned char*)(uintptr_t)ADDR_TURTLE_HOME;
    unsigned char* turtle_state =
        (unsigned char*)(uintptr_t)ADDR_TURTLE_STATE;
    unsigned char saved_turtle_home[TURTLE_STATE_SIZE];
    unsigned char saved_turtle_state[TURTLE_STATE_SIZE];
    float scale;
    int saved_mouse_timeout;

#if !defined(__GNUC__) || !defined(__i386__)
    (void)x;
    (void)y;
    return 0;
#else
    if (IsBadCodePtr((FARPROC)(uintptr_t)ADDR_CURSOR_DRAW) ||
        IsBadCodePtr((FARPROC)(uintptr_t)ADDR_TURTLE_RESET) ||
        IsBadCodePtr((FARPROC)(uintptr_t)ADDR_TURTLE_SET_SCALE) ||
        IsBadReadPtr((const void*)global_scale, sizeof(*global_scale)) ||
        IsBadWritePtr((void*)mouse_timeout, sizeof(*mouse_timeout)) ||
        IsBadReadPtr(turtle_home, TURTLE_STATE_SIZE) ||
        IsBadWritePtr(turtle_home, TURTLE_STATE_SIZE) ||
        IsBadReadPtr(turtle_state, TURTLE_STATE_SIZE) ||
        IsBadWritePtr(turtle_state, TURTLE_STATE_SIZE)) {
        return 0;
    }
    scale = *global_scale;
    if (!(scale > 0.0f) || scale > FLT_MAX) return 0;

    memcpy(saved_turtle_home, turtle_home, TURTLE_STATE_SIZE);
    memcpy(saved_turtle_state, turtle_state, TURTLE_STATE_SIZE);

    /* turtle_reset zeroes scale as well as transform/color. Vanilla main-menu
     * rendering establishes _global_scale before cursor_draw; reproduce that
     * precondition explicitly because framework overlays run after main_draw. */
    turtle_reset();
    turtle_set_scale((double)scale);

    /* Online/custom states consume SDL motion themselves, so native main menu
     * navigation may leave _mouse_timeout at zero. Preserve the framework's
     * established always-visible cursor policy without leaking a native state
     * change: make both native passes visible, then restore the exact value. */
    saved_mouse_timeout = *mouse_timeout;
    if (saved_mouse_timeout <= 0) *mouse_timeout = 1;

    /* This is the native main_draw order. EDX remains zero (mouse mode): the
     * first pass supplies the offset black 0.75-alpha shadow, and the second
     * supplies the _mad_ticks-driven red/yellow pulse. cursor_draw itself owns
     * the hotspot transform, misc[7] lookup, timeout, and 96-byte turtle save. */
    cursor_ext_native_mouse_pass(x, y, 0.0f, 1);
    cursor_ext_native_mouse_pass(x, y, 0.0f, 0);
    *mouse_timeout = saved_mouse_timeout;
    memcpy(turtle_state, saved_turtle_state, TURTLE_STATE_SIZE);
    memcpy(turtle_home, saved_turtle_home, TURTLE_STATE_SIZE);
    return 1;
#endif
}

int cursor_ext_call_with_vanilla_mouse_hidden(cursor_ext_draw_callback draw) {
#if !defined(__GNUC__) || !defined(__i386__)
    (void)draw;
    return 0;
#else
    volatile int* mouse_timeout =
        (volatile int*)(uintptr_t)ADDR_MOUSE_TIMEOUT;
    int saved_mouse_timeout;

    if (!draw ||
        IsBadCodePtr((FARPROC)(uintptr_t)draw) ||
        IsBadWritePtr((void*)mouse_timeout, sizeof(*mouse_timeout))) {
        return 0;
    }
    saved_mouse_timeout = *mouse_timeout;
    *mouse_timeout = 0;
    draw();
    *mouse_timeout = saved_mouse_timeout;
    return 1;
#endif
}
