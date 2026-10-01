#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Include the actual transport so the test can shrink its OS send buffer. */
#include "../net_ext.c"

int main(int argc, char** argv) {
    int slot, state = 0;
    DWORD started;
    char data[32768];
    if (argc != 5) return 2;
    if (strcmp(argv[4], "-") != 0) {
        unsigned char der[8192];
        FILE* root = fopen(argv[4], "rb");
        size_t count;
        if (!root) return 2;
        count = fread(der, 1, sizeof(der), root);
        fclose(root);
        if (!count || !net_tls_test_root(der, (unsigned int)count)) return 2;
    }
    slot = net_connect_control(argv[1], atoi(argv[2]), 1);
    if (slot < 0) return 3;
    started = GetTickCount();
    while ((DWORD)(GetTickCount() - started) < 15000u) {
        state = net_check_connect(slot);
        if (state) break;
        if (net_connected(slot)) return 4; /* never publish an unfinished handshake */
        Sleep(1);
    }
    if (strcmp(argv[3], "reject") == 0) {
        int passed = state == -1 && !net_connected(slot) && net_send(slot, "PASSWORD", 8) < 0;
        net_close(slot);
        net_tls_test_root(NULL, 0);
        if (passed) { puts("TLS rejection before account traffic: PASS"); return 0; }
        return 5;
    }
    if (state != 1) { fprintf(stderr, "%s\n", net_last_error()); return 6; }
    if (strcmp(argv[3], "http") == 0) {
        int count = snprintf(data, sizeof(data),
            "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", argv[1]);
        if (net_send(slot, data, count) != count) return 7;
        started = GetTickCount();
        while ((DWORD)(GetTickCount() - started) < 15000u) {
            count = net_recv(slot, data, sizeof(data));
            if (count > 0) {
                if (count < 5 || memcmp(data, "HTTP/", 5)) return 8;
                net_close(slot);
                puts("Windows automatic certificate/hostname verification: PASS");
                return 0;
            }
            if (count < 0) return 9;
            Sleep(1);
        }
        return 10;
    }
    {
        const unsigned int total = 2u * 1024u * 1024u;
        unsigned int sent = 0, received = 0;
        int backpressure = 0, tiny = 512;
        setsockopt(g_net[slot].fd, SOL_SOCKET, SO_SNDBUF, (char*)&tiny, sizeof(tiny));
        started = GetTickCount();
        while (received < total && (DWORD)(GetTickCount() - started) < 30000u) {
            if (sent < total) {
                unsigned int count = total - sent;
                int accepted;
                if (count > sizeof(data)) count = sizeof(data);
                for (unsigned int i = 0; i < count; i++) data[i] = (char)(((sent + i) * 37u + 11u) & 255u);
                accepted = net_send(slot, data, (int)count);
                if (accepted < 0) return 11;
                if (accepted == 0) backpressure++;
                else { if ((unsigned int)accepted != count) return 12; sent += count; }
                memset(data, 0, sizeof(data)); /* copied message lifetime */
            }
            {
                int got = net_recv(slot, data, 113); /* partial decrypted records */
                if (got < 0) return 13;
                for (int i = 0; i < got; i++) {
                    if ((unsigned char)data[i] != (((received + (unsigned int)i) * 37u + 11u) & 255u)) return 14;
                }
                received += (unsigned int)got;
                if (!got && sent == total) Sleep(1);
            }
        }
        net_close(slot);
        net_tls_test_root(NULL, 0);
        if (sent != total || received != total || !backpressure) return 15;
        printf("TLS 2MiB copied send/receive under backpressure: PASS (refusals=%d)\n", backpressure);
    }
    return 0;
}
