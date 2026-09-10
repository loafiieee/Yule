#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef long double (__cdecl *fn_rng_frnd_t)(float, float);
static uintptr_t test_caller;
static uint32_t game_seed;
static uint32_t* g_native_mrand_seed = &game_seed;
static uint32_t g_cosmetic_rng_seed = 123u;
static int g_cosmetic_rng_depth, g_audio_rng_wrap_depth, trace_calls;
static int hooks_on_audio_thread(void) { return 0; }
static uint32_t hooks_audio_rng_step(void) { return 0u; }
static void hooks_rng_trace_record_from_hook(uint32_t kind, uintptr_t caller) {
    (void)kind; (void)caller; trace_calls++;
}
static uint32_t native_step(uint32_t s) {
    s = s * 0x41c64e6du + 0x3039u;
    return (s >> 1) ^ s ^ ((0u - (s & 1u)) & 0xd0000001u);
}
static long double __cdecl native_frnd(float lo, float hi) {
    game_seed = native_step(game_seed);
    return ((long double)(game_seed >> 16) * (hi - lo)) / 65535.0L + lo;
}
static fn_rng_frnd_t g_hooks_rng_frnd_trampoline = native_frnd;
#include "../build/rng_cosmetic_under_test.h"
int main(void) {
    /* Both randomly selected creepy-pitch paths must use cosmetic RNG. */
    assert(hooks_rng_caller_is_cosmetic(0x4240ffu));
    assert(hooks_rng_caller_is_cosmetic(0x42411bu));
    assert(hooks_rng_caller_is_cosmetic(0x424410u));
    assert(hooks_rng_caller_is_cosmetic(0x42413fu));
    /* Do not widen the exception across nearby movement/gameplay code. */
    assert(!hooks_rng_caller_is_cosmetic(0x42440fu));
    assert(!hooks_rng_caller_is_cosmetic(0x424411u));
    assert(!hooks_rng_caller_is_cosmetic(0x424380u));
    assert(!hooks_rng_caller_is_cosmetic(0x42ca16u));
    assert(!hooks_rng_caller_is_cosmetic(0x42ca2cu));
    /* Replay the captured RNG prefix, then execute the real frnd hook. */
    game_seed = native_step(native_step(672121905u));
    assert(game_seed == 1129937819u);
    assert(native_step(game_seed) == 2541194916u);
    test_caller = 0x424410u;
    (void)hooked_frnd(0.0f, 45.0f);
    assert(game_seed == 1129937819u);
    assert(g_cosmetic_rng_seed == native_step(123u));
    assert(g_cosmetic_rng_depth == 0 && trace_calls == 0);
    test_caller = 0x42ca16u;
    (void)hooked_frnd(0.0f, 1.0f);
    assert(game_seed == 2541194916u && trace_calls == 1);
    puts("Native cosmetic RNG branch classification: OK");
    return 0;
}
