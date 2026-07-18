#include <windows.h>
#include "log.h"
#include "hooks.h"
#include "lua_manager.h"
#include "custom_maps.h"
#include "content_tiles.h"


#include <stdint.h>

// ---------------- Crash handler (writes mods/crash.log, optional minidump) ----------------

typedef enum _MINIDUMP_TYPE {
    MiniDumpNormal = 0x00000000
} MINIDUMP_TYPE;

typedef struct _MINIDUMP_EXCEPTION_INFORMATION {
    DWORD ThreadId;
    PEXCEPTION_POINTERS ExceptionPointers;
    BOOL ClientPointers;
} MINIDUMP_EXCEPTION_INFORMATION;

typedef BOOL (WINAPI *MiniDumpWriteDump_t)(
    HANDLE hProcess,
    DWORD ProcessId,
    HANDLE hFile,
    MINIDUMP_TYPE DumpType,
    const MINIDUMP_EXCEPTION_INFORMATION* ExceptionParam,
    const void* UserStreamParam,
    const void* CallbackParam
);

static LONG WINAPI luna_unhandled_exception_filter(EXCEPTION_POINTERS* ep);
static LONG CALLBACK luna_vectored_exception_handler(EXCEPTION_POINTERS* ep);

#define LUNA_FORCED_CRASH_EXCEPTION ((DWORD)0xE14E4747u)

static PVOID g_luna_vectored_handler = NULL;
static LONG  g_luna_crash_reporting = 0;

static void crash_append_line(const char* line) {
    // Best-effort, no CRT FILE* dependency.
    CreateDirectoryA("mods", NULL);
    HANDLE h = CreateFileA(
        "mods/crash.log",
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, line, (DWORD)lstrlenA(line), &written, NULL);
    WriteFile(h, "\r\n", 2, &written, NULL);
    CloseHandle(h);
}

static void try_write_minidump(EXCEPTION_POINTERS* ep) {
    HMODULE dbg = LoadLibraryA("dbghelp.dll");
    if (!dbg) return;

    MiniDumpWriteDump_t pMiniDumpWriteDump = (MiniDumpWriteDump_t)GetProcAddress(dbg, "MiniDumpWriteDump");
    if (!pMiniDumpWriteDump) {
        FreeLibrary(dbg);
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    char dump_path[MAX_PATH];
    wsprintfA(dump_path, "mods/crash_%04d%02d%02d_%02d%02d%02d.dmp",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileA(dump_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        FreeLibrary(dbg);
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    HANDLE hProc = GetCurrentProcess();
    DWORD pid = GetCurrentProcessId();

    pMiniDumpWriteDump(hProc, pid, hFile, MiniDumpNormal, &mei, NULL, NULL);
    CloseHandle(hFile);
    FreeLibrary(dbg);
}

void luna_reinstall_crash_handler(void) {
    SetUnhandledExceptionFilter(luna_unhandled_exception_filter);
}

void luna_force_crash_report(unsigned int exit_code) {
    ULONG_PTR args[1];
    args[0] = (ULONG_PTR)exit_code;
    RaiseException(LUNA_FORCED_CRASH_EXCEPTION, EXCEPTION_NONCONTINUABLE, 1, args);
    TerminateProcess(GetCurrentProcess(), exit_code);
}

static void install_crash_handler(void) {
    luna_reinstall_crash_handler();
    if (!g_luna_vectored_handler) {
        g_luna_vectored_handler = AddVectoredExceptionHandler(1, luna_vectored_exception_handler);
    }
}

static const char* exception_code_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
        case LUNA_FORCED_CRASH_EXCEPTION: return "FORCED_CRASH";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        default: return "UNKNOWN";
    }
}

static int should_report_vectored_exception(DWORD code) {
    switch (code) {
        case LUNA_FORCED_CRASH_EXCEPTION:
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_STACK_OVERFLOW:
            return 1;
        default:
            return 0;
    }
}

static void crash_report_exception(EXCEPTION_POINTERS* ep, const char* label) {
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : NULL;

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[512];
    wsprintfA(line,
              "[%04d-%02d-%02d %02d:%02d:%02d] %s 0x%08lX (%s) at %p",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
              label ? label : "Unhandled exception",
              (unsigned long)code, exception_code_name(code), addr);
    crash_append_line(line);

    // Optional minidump if dbghelp.dll is present.
    try_write_minidump(ep);
    crash_append_line("Wrote minidump if available: mods/crash_YYYYMMDD_HHMMSS.dmp");

    // Also try to surface a visible error (best-effort).
    MessageBoxA(NULL,
        "Yule Crashed.\n\n"
        "Details were written to mods/crash.log (and a .dmp if possible).\n"
        "Please attach those files when reporting bugs.",
        "Eggnogg+ Crash",
        MB_OK | MB_ICONERROR
    );
}

static LONG CALLBACK luna_vectored_exception_handler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    UINT exit_code = 1;

    if (!should_report_vectored_exception(code)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedCompareExchange(&g_luna_crash_reporting, 1, 0) != 0) {
        TerminateProcess(GetCurrentProcess(), code == LUNA_FORCED_CRASH_EXCEPTION ? exit_code : (UINT)code);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (code == LUNA_FORCED_CRASH_EXCEPTION &&
        ep && ep->ExceptionRecord && ep->ExceptionRecord->NumberParameters >= 1) {
        exit_code = (UINT)ep->ExceptionRecord->ExceptionInformation[0];
    } else if (code != 0) {
        exit_code = (UINT)code;
    }

    crash_report_exception(ep, code == LUNA_FORCED_CRASH_EXCEPTION ? "Forced crash" : "Vectored exception");
    TerminateProcess(GetCurrentProcess(), exit_code);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI luna_unhandled_exception_filter(EXCEPTION_POINTERS* ep) {
    crash_report_exception(ep, "Unhandled exception");
    return EXCEPTION_EXECUTE_HANDLER;
}

// -----------------------------------------------------------------------------------------

// Minimal SDL event structures
typedef unsigned int Uint32;
typedef unsigned short Uint16;
typedef unsigned char Uint8;
typedef short Sint16;

typedef struct {
    int scancode;
    int sym;
    Uint16 mod;
    Uint32 unused;
} SDL_Keysym;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2;
    Uint8 padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 event;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    int data1;
    int data2;
} SDL_WindowEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 clicks;
    Uint8 padding1;
    int x;
    int y;
} SDL_MouseButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint32 state;
    int x;
    int y;
    int xrel;
    int yrel;
} SDL_MouseMotionEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    int x;
    int y;
    Uint32 direction;
} SDL_MouseWheelEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[32];
} SDL_TextInputEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_ControllerAxisEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
} SDL_ControllerButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_JoyAxisEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
} SDL_JoyButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 hat;
    Uint8 value;
    Uint8 padding1;
    Uint8 padding2;
} SDL_JoyHatEvent;

typedef union {
    Uint32 type;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
    SDL_MouseWheelEvent wheel;
    SDL_TextInputEvent text;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyHatEvent jhat;
    unsigned char padding[56];
} SDL_Event;

#define SDL_KEYDOWN         0x300
#define SDL_KEYUP           0x301
#define SDL_TEXTINPUT       0x303
#define SDL_QUIT            0x100
#define SDL_WINDOWEVENT     0x200
#define SDL_MOUSEMOTION     0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP   0x402
#define SDL_MOUSEWHEEL      0x403
#define SDL_CONTROLLERAXISMOTION 0x650
#define SDL_CONTROLLERBUTTONDOWN 0x651
#define SDL_CONTROLLERBUTTONUP   0x652
#define SDL_JOYAXISMOTION        0x600
#define SDL_JOYBUTTONDOWN       0x603
#define SDL_JOYBUTTONUP         0x604
#define SDL_JOYHATMOTION        0x602

#define SDL_CONTROLLER_BUTTON_A            0
#define SDL_CONTROLLER_BUTTON_B            1
#define SDL_CONTROLLER_BUTTON_X            2
#define SDL_CONTROLLER_BUTTON_Y            3
#define SDL_CONTROLLER_BUTTON_BACK         4
#define SDL_CONTROLLER_BUTTON_DPAD_UP     11
#define SDL_CONTROLLER_BUTTON_DPAD_DOWN   12
#define SDL_CONTROLLER_BUTTON_DPAD_LEFT   13
#define SDL_CONTROLLER_BUTTON_DPAD_RIGHT  14

#define SDL_HAT_UP    0x01
#define SDL_HAT_RIGHT 0x02
#define SDL_HAT_DOWN  0x04
#define SDL_HAT_LEFT  0x08

typedef void SDL_Window;
typedef void (*SDL_GL_SwapWindow_t)(SDL_Window*);
typedef int  (*SDL_PollEvent_t)(SDL_Event*);

HMODULE real_sdl = NULL;
static SDL_GL_SwapWindow_t real_SwapWindow = NULL;
static SDL_PollEvent_t     real_PollEvent  = NULL;

// ---------------- Time scaling (speedhack support) ----------------
// Mods can modify a synthetic "delta_time" event each frame. We turn that
// into a multiplier that scales SDL time sources (GetTicks / QPC wrappers).

static LARGE_INTEGER g_qpc_freq;
static int g_qpc_inited = 0;

// Piecewise-continuous scaled clock so changing scale doesn't jump.
static unsigned long long g_real_anchor = 0;
static double g_scaled_anchor = 0.0;
static float g_last_scale = 1.0f;

// For generating delta_time (real, unscaled)
static unsigned long long g_last_real_qpc = 0;

static unsigned long long luna_real_qpc(void) {
    if (!g_qpc_inited) {
        QueryPerformanceFrequency(&g_qpc_freq);
        g_qpc_inited = 1;
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        g_real_anchor = (unsigned long long)c.QuadPart;
        g_scaled_anchor = 0.0;
        g_last_scale = 1.0f;
        g_last_real_qpc = (unsigned long long)c.QuadPart;
        return (unsigned long long)c.QuadPart;
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (unsigned long long)c.QuadPart;
}

static unsigned long long luna_scaled_qpc(void) {
    unsigned long long now = luna_real_qpc();
    float scale = lua_manager_get_time_scale();
    if (scale < 0.05f) scale = 0.05f;
    if (scale > 100.0f)  scale = 100.0f;

    if (scale != g_last_scale) {
        double dreal = (double)(now - g_real_anchor);
        g_scaled_anchor += dreal * (double)g_last_scale;
        g_real_anchor = now;
        g_last_scale = scale;
    }

    double dreal = (double)(now - g_real_anchor);
    double scaled = g_scaled_anchor + dreal * (double)g_last_scale;
    return (unsigned long long)scaled;
}

// These wrappers are wired up by init_stubs() by overriding stub pointers.
unsigned int __cdecl luna_SDL_GetTicks(void) {
    unsigned long long qpc = luna_scaled_qpc();
    double seconds = (double)qpc / (double)g_qpc_freq.QuadPart;
    return (unsigned int)(seconds * 1000.0);
}

unsigned long long __cdecl luna_SDL_GetPerformanceCounter(void) {
    return luna_scaled_qpc();
}

unsigned long long __cdecl luna_SDL_GetPerformanceFrequency(void) {
    if (!g_qpc_inited) (void)luna_real_qpc();
    return (unsigned long long)g_qpc_freq.QuadPart;
}

// ---------------------------------------------------------------

void init_stubs();
void lua_manager_init();
void lua_manager_on_frame();
int  lua_manager_on_event(const char*, int, int, int, int, int, int);
void lua_manager_on_key_event(int sym, int is_down);
void lua_manager_shutdown();

void SDL_GL_SwapWindow(SDL_Window* window) {
    luna_reinstall_crash_handler();

    // Generate a real (unscaled) delta_time and let mods adjust it.
    // This feeds the time-scale multiplier used by the SDL time wrappers.
    unsigned long long now = luna_real_qpc();
    if (g_qpc_inited && g_last_real_qpc != 0) {
        double dt = (double)(now - g_last_real_qpc) / (double)g_qpc_freq.QuadPart;
        (void)lua_manager_on_delta_time(dt);
    }
    g_last_real_qpc = now;

    hooks_window_on_pre_swap();
    lua_manager_on_frame();
    hooks_online_on_pre_swap();
    hooks_console_on_pre_swap();
    hooks_update_on_pre_swap();
    hooks_online_cursor_on_pre_swap();
    if (real_SwapWindow) real_SwapWindow(window);
}

int SDL_PollEvent(SDL_Event* event) {
    if (!real_PollEvent) return 0;
    luna_reinstall_crash_handler();
    hooks_console_pump();

    // Allow Lua mods to "consume" SDL events by returning true from an on_event handler.
    // If consumed, we keep polling until we find a non-consumed event (or the queue is empty).
    while (1) {
        int result = real_PollEvent(event);
        if (!result || !event) return result;

        int consumed = 0;
        switch (event->type) {
            case SDL_WINDOWEVENT:
                hooks_window_event((int)event->window.event,
                                   event->window.data1,
                                   event->window.data2);
                consumed = 0;
                break;
            case SDL_KEYDOWN:
                if (hooks_console_keydown(event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.mod)) {
                    consumed = 1;
                    break;
                }
                if (hooks_window_keydown(event->key.keysym.sym, event->key.repeat)) {
                    consumed = 1;
                    break;
                }
                // If we're capturing text for an in-game config string field or bind row, swallow key presses here.
                if (hooks_text_capture_active() &&
                    hooks_text_capture_keydown(event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.mod)) {
                    consumed = 1;
                    break;
                }
                if (hooks_mods_menu_keydown(event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.mod)) {
                    consumed = 1;
                    break;
                }
                if (hooks_online_hub_keydown(event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.mod)) {
                    consumed = 1;
                    break;
                }
                lua_manager_on_key_event(event->key.keysym.sym, 1);
                consumed = lua_manager_on_event("keydown",
                    event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.mod, 0, 0, 0);
                break;
            case SDL_KEYUP:
                if (hooks_console_active()) {
                    consumed = 1;
                    break;
                }
                lua_manager_on_key_event(event->key.keysym.sym, 0);
                consumed = lua_manager_on_event("keyup",
                    event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.mod, 0, 0, 0);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (hooks_update_mousebutton(event->button.x, event->button.y,
                                             event->button.button, 1)) {
                    consumed = 1;
                    break;
                }
                if (hooks_console_active()) {
                    hooks_console_mousebutton(event->button.x, event->button.y, event->button.button, 1);
                    consumed = 1;
                    break;
                }
                if (hooks_online_hub_mousebutton(event->button.x, event->button.y, event->button.button, 1)) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("mousebuttondown",
                    0, 0, 0, event->button.x, event->button.y, event->button.button);
                break;
            case SDL_MOUSEBUTTONUP:
                if (hooks_console_active()) {
                    hooks_console_mousebutton(event->button.x, event->button.y, event->button.button, 0);
                    consumed = 1;
                    break;
                }
                if (hooks_online_hub_mousebutton(event->button.x, event->button.y, event->button.button, 0)) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("mousebuttonup",
                    0, 0, 0, event->button.x, event->button.y, event->button.button);
                break;
            case SDL_MOUSEMOTION:
                if (hooks_console_active()) {
                    consumed = 1;
                    break;
                }
                if (hooks_online_hub_mousemotion(event->motion.x, event->motion.y)) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("mousemotion",
                    0, 0, 0, event->motion.x, event->motion.y, 0);
                break;
            case SDL_MOUSEWHEEL:
                if (hooks_console_active()) {
                    hooks_console_mousewheel(event->wheel.y);
                    consumed = 1;
                    break;
                }
                if (hooks_online_hub_mousewheel(event->wheel.y)) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("mousewheel",
                    0, 0, 0, event->wheel.x, event->wheel.y, (int)event->wheel.direction);
                break;
            case SDL_TEXTINPUT: {
                if (hooks_console_textinput(event->text.text)) {
                    consumed = 1;
                    break;
                }
                if (hooks_online_hub_textinput(event->text.text)) {
                    consumed = 1;
                    break;
                }
                int first_ch = (unsigned char)event->text.text[0];
                consumed = lua_manager_on_event("textinput",
                    first_ch, 0, 0, 0, 0, 0);
                break;
            }
            case SDL_CONTROLLERAXISMOTION:
                if (hooks_console_active()) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("controlleraxismotion",
                    (int)event->caxis.axis, 0, 0, (int)event->caxis.which, (int)event->caxis.value, 0);
                break;
            case SDL_CONTROLLERBUTTONDOWN: {
                int action = 0;
                if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) action = 1;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) action = 2;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) action = 3;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) action = 4;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_A) action = 5;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_B) action = 5;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_X) action = 5;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_Y) action = 5;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) action = 6;

                if (action && hooks_console_control_action(action)) {
                    consumed = 1;
                } else if (hooks_console_active()) {
                    consumed = 1;
                } else if (action && hooks_mods_menu_control_action(action)) {
                    consumed = 1;
                } else if (action && hooks_online_hub_control_action(action)) {
                    consumed = 1;
                } else {
                    consumed = lua_manager_on_event("controllerbuttondown",
                        (int)event->cbutton.button, 0, 0, (int)event->cbutton.which, (int)event->cbutton.state, 0);
                }
                break;
            }
            case SDL_CONTROLLERBUTTONUP:
                if (hooks_console_active()) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("controllerbuttonup",
                    (int)event->cbutton.button, 0, 0, (int)event->cbutton.which, (int)event->cbutton.state, 0);
                break;
            case SDL_JOYAXISMOTION:
                if (hooks_console_active()) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("joyaxismotion",
                    (int)event->jaxis.axis, 0, 0, (int)event->jaxis.which, (int)event->jaxis.value, 0);
                break;
            case SDL_JOYBUTTONDOWN: {
                int action = 0;
                if (event->jbutton.button == 0 || event->jbutton.button == 1 ||
                    event->jbutton.button == 2 || event->jbutton.button == 3) {
                    action = 5;
                } else if (event->jbutton.button == 6 || event->jbutton.button == 7) {
                    action = 6;
                }
                if (action && hooks_console_control_action(action)) {
                    consumed = 1;
                } else if (hooks_console_active()) {
                    consumed = 1;
                } else if (action && hooks_mods_menu_control_action(action)) {
                    consumed = 1;
                } else if (action && hooks_online_hub_control_action(action)) {
                    consumed = 1;
                } else {
                    consumed = lua_manager_on_event("joybuttondown",
                        (int)event->jbutton.button, 0, 0, (int)event->jbutton.which, (int)event->jbutton.state, 0);
                }
                break;
            }
            case SDL_JOYBUTTONUP:
                if (hooks_console_active()) {
                    consumed = 1;
                    break;
                }
                consumed = lua_manager_on_event("joybuttonup",
                    (int)event->jbutton.button, 0, 0, (int)event->jbutton.which, (int)event->jbutton.state, 0);
                break;
            case SDL_JOYHATMOTION: {
                int action = 0;
                if (event->jhat.value & SDL_HAT_UP) action = 1;
                else if (event->jhat.value & SDL_HAT_DOWN) action = 2;
                else if (event->jhat.value & SDL_HAT_LEFT) action = 3;
                else if (event->jhat.value & SDL_HAT_RIGHT) action = 4;
                if (action && hooks_console_control_action(action)) {
                    consumed = 1;
                } else if (hooks_console_active()) {
                    consumed = 1;
                } else if (action && hooks_mods_menu_control_action(action)) {
                    consumed = 1;
                } else if (action && hooks_online_hub_control_action(action)) {
                    consumed = 1;
                } else {
                    consumed = lua_manager_on_event("joyhatmotion",
                        (int)event->jhat.hat, 0, 0, (int)event->jhat.which, (int)event->jhat.value, 0);
                }
                break;
            }
            case SDL_QUIT:
                // Notify Lua mods (best-effort) before quit is delivered to the game.
                (void)lua_manager_on_event("quit", 0, 0, 0, 0, 0, 0);
                consumed = 0;
                break;
            default:
                // Unknown/unhandled event type: don't consume
                consumed = 0;
                break;
        }

        if (!consumed) return result;
        // else: keep looping, pulling the next SDL event
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        install_crash_handler();
        real_sdl = LoadLibraryA("SDL2_real.dll");
        if (!real_sdl) {
            MessageBoxA(NULL, "Could not load SDL2_real.dll!", "Error", MB_OK);
            return FALSE;
        }
        real_SwapWindow = (SDL_GL_SwapWindow_t)GetProcAddress(real_sdl, "SDL_GL_SwapWindow");
        real_PollEvent  = (SDL_PollEvent_t)    GetProcAddress(real_sdl, "SDL_PollEvent");
        init_stubs();
        log_init();
        LOG_INFO("Mod framework initializing...");
        lua_manager_init();

        // Game hooks (OPTIONS -> Mods submenu)
        hooks_init();
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        content_tiles_shutdown();
        custom_maps_shutdown();
        lua_manager_shutdown();
        if (real_sdl) FreeLibrary(real_sdl);
    }
    return TRUE;
}
