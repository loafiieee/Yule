#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Installs game-function detours (OPTIONS menu injection, etc.)
void hooks_init(void);

// Returns 1 if the framework is currently capturing text input for a string config field.
int hooks_text_capture_active(void);

// Handle a keydown while text capture is active.
// Returns 1 if consumed (framework handled it), else 0.
int hooks_text_capture_keydown(int sym, int scancode, int mod);

// Handle keydown navigation for the custom MODS menu.
// Returns 1 if consumed, else 0.
int hooks_mods_menu_keydown(int sym, int scancode, int mod);

// Handle controller-style menu actions for the custom MODS menu.
// action: 1=up 2=down 3=left 4=right 5=activate 6=back
// Returns 1 if consumed, else 0.
int hooks_mods_menu_control_action(int action);

#ifdef __cplusplus
}
#endif
