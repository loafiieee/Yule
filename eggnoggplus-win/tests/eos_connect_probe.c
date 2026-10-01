#include "../eos_runtime.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    YuleEosConfig config = {
        "a90d288fedff4672b082ed3e92a12484",
        "02dd8fe04e294815881f54986c73629a",
        "746ee96def484dcc8bbc9806343352b2",
        "xyza7891PRKu95tnw9S2svEZhSaWbVkn", NULL
    };
    char token[256], jwt[16384], error[256] = {0};
    DWORD start;
    int result = 1;
    config.client_secret = getenv("YULE_TEST_EOS_CLIENT_SECRET");
    if (!fgets(token, sizeof(token), stdin)) return 1;
    token[strcspn(token, "\r\n")] = '\0';
    if (!yule_eos_initialize(&config, error, sizeof(error)) ||
        !yule_eos_connect_login(token, error, sizeof(error))) goto done;
    SecureZeroMemory(token, sizeof(token));
    start = GetTickCount();
    while (yule_eos_state() == YULE_EOS_CONNECT_PENDING &&
           (DWORD)(GetTickCount() - start) < 20000u) {
        yule_eos_tick();
        Sleep(10);
    }
    if (yule_eos_state() != YULE_EOS_CONNECT_READY ||
        !yule_eos_copy_id_token(jwt, sizeof(jwt))) goto done;
    /* Captured by the guarded runner; never forward this output to logs. */
    printf("YULE_EOS_PROOF:%s %s\n", yule_eos_puid(), jwt);
    result = 0;
done:
    SecureZeroMemory(token, sizeof(token));
    SecureZeroMemory(jwt, sizeof(jwt));
    yule_eos_shutdown();
    if (result) fprintf(stderr, "EOS Connect probe failed\n");
    return result;
}
