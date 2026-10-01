/* Windows Schannel TLS 1.2, with normal Windows certificate-chain and hostname
 * verification. No manual-validation, insecure-certificate or downgrade mode. */
#define SECURITY_WIN32
#include "net_tls.h"
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <stdlib.h>
#include <string.h>
#ifdef NET_TLS_TEST
#include <wincrypt.h>
static HCERTSTORE g_test_root;
int net_tls_test_root(const unsigned char* der, unsigned int len) {
    if (g_test_root) CertCloseStore(g_test_root, 0);
    g_test_root = NULL;
    if (!der) return 1;
    g_test_root = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    return g_test_root && CertAddEncodedCertificateToStore(g_test_root,
        X509_ASN_ENCODING, der, len, CERT_STORE_ADD_ALWAYS, NULL);
}
#endif

#define TLS_BUFFER_CAP (128u * 1024u)
#define TLS_PLAIN_CAP (16u * 1024u)

struct NetTls {
    CredHandle credentials;
    CtxtHandle context;
    int credentials_valid, context_valid, handshake_done, need_rx;
    char hostname[256];
    unsigned long error;
    SecPkgContext_StreamSizes sizes;
    unsigned int rx_len, tx_offset, tx_len, plain_offset, plain_len;
    unsigned char rx[TLS_BUFFER_CAP], tx[TLS_BUFFER_CAP], plain[TLS_PLAIN_CAP];
};

static int fail(NetTls* tls, unsigned long error) {
    tls->error = error;
    return -1;
}

#ifdef NET_TLS_TEST
static int validate_test_certificate(NetTls* tls) {
    PCCERT_CONTEXT cert = NULL;
    PCCERT_CHAIN_CONTEXT chain = NULL;
    HCERTCHAINENGINE engine = NULL;
    CERT_CHAIN_ENGINE_CONFIG config = {0};
    CERT_CHAIN_PARA request = {0};
    CERT_CHAIN_POLICY_PARA policy = {0};
    CERT_CHAIN_POLICY_STATUS result = {0};
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl = {0};
    wchar_t hostname[256];
    LPSTR usage = szOID_PKIX_KP_SERVER_AUTH;
    int valid = 0;
    config.cbSize = sizeof(config);
    config.hExclusiveRoot = g_test_root;
    request.cbSize = sizeof(request);
    request.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    request.RequestedUsage.Usage.cUsageIdentifier = 1;
    request.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
    policy.cbSize = sizeof(policy);
    policy.pvExtraPolicyPara = &ssl;
    ssl.cbSize = sizeof(ssl);
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.pwszServerName = hostname;
    result.cbSize = sizeof(result);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, tls->hostname, -1,
                           hostname, 256) &&
        QueryContextAttributesA(&tls->context, SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                &cert) == SEC_E_OK &&
        CertCreateCertificateChainEngine(&config, &engine) &&
        CertGetCertificateChain(engine, cert, NULL, cert->hCertStore, &request,
                                CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, NULL, &chain) &&
        CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &result))
        valid = result.dwError == 0;
    if (chain) CertFreeCertificateChain(chain);
    if (engine) CertFreeCertificateChainEngine(engine);
    if (cert) CertFreeCertificateContext(cert);
    if (!valid) fail(tls, result.dwError ? result.dwError : SEC_E_UNTRUSTED_ROOT);
    return valid;
}
#endif

NetTls* net_tls_create(const char* hostname) {
    SCHANNEL_CRED config;
    TimeStamp expires;
    SECURITY_STATUS status;
    NetTls* tls;
    if (!hostname || !hostname[0] || strlen(hostname) >= 256u) return NULL;
    tls = (NetTls*)calloc(1, sizeof(*tls));
    if (!tls) return NULL;
    strcpy(tls->hostname, hostname);
    SecInvalidateHandle(&tls->context);
    memset(&config, 0, sizeof(config));
    config.dwVersion = SCHANNEL_CRED_VERSION;
    config.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    config.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS |
                     SCH_USE_STRONG_CRYPTO;
#ifdef NET_TLS_TEST
    if (g_test_root) config.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION |
        SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
#endif
    status = AcquireCredentialsHandleA(NULL, UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
        NULL, &config, NULL, NULL, &tls->credentials, &expires);
    if (status != SEC_E_OK) { free(tls); return NULL; }
    tls->credentials_valid = 1;
    return tls;
}

int net_tls_flush(NetTls* tls, SOCKET socket) {
    while (tls->tx_len) {
        int sent = send(socket, (char*)tls->tx + tls->tx_offset, (int)tls->tx_len, 0);
        if (sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) return 0;
            return fail(tls, (unsigned long)error);
        }
        if (sent <= 0) return fail(tls, WSAECONNRESET);
        tls->tx_offset += (unsigned int)sent;
        tls->tx_len -= (unsigned int)sent;
    }
    tls->tx_offset = 0;
    return 1;
}

static int receive_wire(NetTls* tls, SOCKET socket) {
    int got;
    if (tls->rx_len == TLS_BUFFER_CAP) return fail(tls, SEC_E_BUFFER_TOO_SMALL);
    got = recv(socket, (char*)tls->rx + tls->rx_len,
               (int)(TLS_BUFFER_CAP - tls->rx_len), 0);
    if (got == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return 0;
        return fail(tls, (unsigned long)error);
    }
    if (got <= 0) return fail(tls, SEC_I_CONTEXT_EXPIRED);
    tls->rx_len += (unsigned int)got;
    return 1;
}

int net_tls_handshake(NetTls* tls, SOCKET socket) {
    const ULONG flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
        ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    for (unsigned int step = 0; step < 16u; step++) {
        SecBuffer input[2] = {{0}}, output = {0};
        SecBufferDesc in = {SECBUFFER_VERSION, 2, input};
        SecBufferDesc out = {SECBUFFER_VERSION, 1, &output};
        TimeStamp expires;
        ULONG attributes = 0;
        SECURITY_STATUS status;
        int flushed = net_tls_flush(tls, socket);
        if (flushed <= 0) return flushed;
        if (tls->handshake_done) return 1;
        if (tls->context_valid && (tls->need_rx || !tls->rx_len)) {
            int got = receive_wire(tls, socket);
            if (got <= 0) return got;
            tls->need_rx = 0;
        }
        input[0].BufferType = SECBUFFER_TOKEN;
        input[0].pvBuffer = tls->rx;
        input[0].cbBuffer = tls->rx_len;
        input[1].BufferType = SECBUFFER_EMPTY;
        output.BufferType = SECBUFFER_TOKEN;
        status = InitializeSecurityContextA(&tls->credentials,
            tls->context_valid ? &tls->context : NULL, tls->hostname, flags,
            0, SECURITY_NATIVE_DREP, tls->context_valid ? &in : NULL, 0,
            &tls->context, &out, &attributes, &expires);
        tls->context_valid = SecIsValidHandle(&tls->context);
        if (output.cbBuffer) {
            if (output.cbBuffer > TLS_BUFFER_CAP) {
                FreeContextBuffer(output.pvBuffer);
                return fail(tls, SEC_E_BUFFER_TOO_SMALL);
            }
            memcpy(tls->tx, output.pvBuffer, output.cbBuffer);
            tls->tx_len = output.cbBuffer;
        }
        if (output.pvBuffer) FreeContextBuffer(output.pvBuffer);
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            tls->need_rx = 1;
            return 0;
        }
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED)
            return fail(tls, (unsigned long)status);
        if (input[1].BufferType == SECBUFFER_EXTRA) {
            if (input[1].cbBuffer > tls->rx_len) return fail(tls, SEC_E_INTERNAL_ERROR);
            memmove(tls->rx, tls->rx + tls->rx_len - input[1].cbBuffer,
                    input[1].cbBuffer);
            tls->rx_len = input[1].cbBuffer;
        } else tls->rx_len = 0;
        if (status == SEC_E_OK) {
#ifdef NET_TLS_TEST
            if (g_test_root && !validate_test_certificate(tls)) return -1;
#endif
            status = QueryContextAttributesA(&tls->context, SECPKG_ATTR_STREAM_SIZES,
                                              &tls->sizes);
            if (status != SEC_E_OK) return fail(tls, (unsigned long)status);
            tls->handshake_done = 1;
        }
    }
    return 0;
}

int net_tls_send(NetTls* tls, SOCKET socket, const char* data, int len) {
    SecBuffer buffers[4] = {{0}};
    SecBufferDesc message = {SECBUFFER_VERSION, 4, buffers};
    unsigned int count;
    SECURITY_STATUS status;
    int flushed = net_tls_flush(tls, socket);
    if (flushed <= 0) return flushed;
    if (len <= 0 || !data || !tls->handshake_done) return fail(tls, SEC_E_INVALID_HANDLE);
    count = (unsigned int)len;
    if (count > tls->sizes.cbMaximumMessage) count = tls->sizes.cbMaximumMessage;
    if (tls->sizes.cbHeader + count + tls->sizes.cbTrailer > TLS_BUFFER_CAP)
        return fail(tls, SEC_E_BUFFER_TOO_SMALL);
    buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    buffers[0].pvBuffer = tls->tx;
    buffers[0].cbBuffer = tls->sizes.cbHeader;
    buffers[1].BufferType = SECBUFFER_DATA;
    buffers[1].pvBuffer = tls->tx + tls->sizes.cbHeader;
    buffers[1].cbBuffer = count;
    memcpy(buffers[1].pvBuffer, data, count);
    buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    buffers[2].pvBuffer = tls->tx + tls->sizes.cbHeader + count;
    buffers[2].cbBuffer = tls->sizes.cbTrailer;
    buffers[3].BufferType = SECBUFFER_EMPTY;
    status = EncryptMessage(&tls->context, 0, &message, 0);
    if (status != SEC_E_OK) return fail(tls, (unsigned long)status);
    tls->tx_len = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
    if (net_tls_flush(tls, socket) < 0) return -1;
    return (int)count; /* tls owns the ciphertext even on socket backpressure. */
}

int net_tls_recv(NetTls* tls, SOCKET socket, char* data, int cap) {
    for (unsigned int step = 0; step < 16u; step++) {
        SecBuffer buffers[4] = {{0}};
        SecBufferDesc message = {SECBUFFER_VERSION, 4, buffers};
        SECURITY_STATUS status;
        unsigned int extra = 0;
        if (tls->plain_len) {
            unsigned int count = tls->plain_len;
            if (count > (unsigned int)cap) count = (unsigned int)cap;
            memcpy(data, tls->plain + tls->plain_offset, count);
            SecureZeroMemory(tls->plain + tls->plain_offset, count);
            tls->plain_offset += count;
            tls->plain_len -= count;
            if (!tls->plain_len) tls->plain_offset = 0;
            return (int)count;
        }
        if (!tls->rx_len || tls->need_rx) {
            int got = receive_wire(tls, socket);
            if (got <= 0) return got;
            tls->need_rx = 0;
        }
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer = tls->rx;
        buffers[0].cbBuffer = tls->rx_len;
        status = DecryptMessage(&tls->context, &message, 0, NULL);
        if (status == SEC_E_INCOMPLETE_MESSAGE) { tls->need_rx = 1; return 0; }
        /* Renegotiation is rejected rather than exposing unauthenticated data. */
        if (status != SEC_E_OK) return fail(tls, (unsigned long)status);
        for (unsigned int i = 0; i < 4u; i++) {
            if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer) {
                if (buffers[i].cbBuffer > TLS_PLAIN_CAP || tls->plain_len)
                    return fail(tls, SEC_E_BUFFER_TOO_SMALL);
                memcpy(tls->plain, buffers[i].pvBuffer, buffers[i].cbBuffer);
                tls->plain_len = buffers[i].cbBuffer;
            } else if (buffers[i].BufferType == SECBUFFER_EXTRA) extra = buffers[i].cbBuffer;
        }
        if (extra > tls->rx_len) return fail(tls, SEC_E_INTERNAL_ERROR);
        if (extra) memmove(tls->rx, tls->rx + tls->rx_len - extra, extra);
        tls->rx_len = extra;
    }
    return 0;
}

unsigned long net_tls_error(const NetTls* tls) { return tls ? tls->error : 0; }
void net_tls_free(NetTls* tls) {
    if (!tls) return;
    if (tls->context_valid) DeleteSecurityContext(&tls->context);
    if (tls->credentials_valid) FreeCredentialsHandle(&tls->credentials);
    SecureZeroMemory(tls, sizeof(*tls));
    free(tls);
}
