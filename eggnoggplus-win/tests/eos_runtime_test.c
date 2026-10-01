#include "../eos_runtime.h"
#include "../eos_sdk_loader.h"
#include "eos_api_list.h"

#include <stdio.h>

int main(void) {
    /* Platform creation does not authenticate an account. No login, tick,
     * bearer token, or private credential is used by this loader test. */
    const YuleEosConfig config = {
        "a90d288fedff4672b082ed3e92a12484",
        "02dd8fe04e294815881f54986c73629a",
        "746ee96def484dcc8bbc9806343352b2",
        "xyza7891PRKu95tnw9S2svEZhSaWbVkn",
        "00000000000000000000000000000000"
    };
    char error[256] = {0};
    size_t i;
    if (!yule_eos_initialize(&config, error, sizeof(error))) {
        fprintf(stderr, "EOS Windows lifecycle test failed: %s\n", error);
        return 1;
    }
    if (yule_eos_state() != YULE_EOS_PLATFORM_READY ||
        yule_eos_puid()[0] || yule_eos_login_inflight()) {
        fprintf(stderr, "EOS platform incorrectly reported account login\n");
        yule_eos_shutdown();
        return 1;
    }
    for (i = 0; i < sizeof(eos_test_apis) / sizeof(eos_test_apis[0]); ++i) {
        if (!yule_eos_sdk_proc((HMODULE)yule_eos_sdk_module(),
                              eos_test_apis[i].name, eos_test_apis[i].bytes)) {
            fprintf(stderr, "Missing SDK entry point: %s\n", eos_test_apis[i].name);
            yule_eos_shutdown();
            return 1;
        }
    }
    yule_eos_shutdown();
    if (yule_eos_state() != YULE_EOS_UNAVAILABLE || yule_eos_sdk_module())
        return 1;
    printf("EOS Win32 load, %u Connect/P2P entry points, and shutdown: PASS\n",
           (unsigned int)i);
    return 0;
}
