#pragma once
#include <winsock2.h>

typedef struct NetTls NetTls;
NetTls* net_tls_create(const char* hostname);
int net_tls_handshake(NetTls* tls, SOCKET socket);
int net_tls_flush(NetTls* tls, SOCKET socket);
int net_tls_send(NetTls* tls, SOCKET socket, const char* data, int len);
int net_tls_recv(NetTls* tls, SOCKET socket, char* data, int cap);
unsigned long net_tls_error(const NetTls* tls);
void net_tls_free(NetTls* tls);
#ifdef NET_TLS_TEST
/* In-memory test trust, never installed into Windows or built into the game. */
int net_tls_test_root(const unsigned char* der, unsigned int len);
#endif
