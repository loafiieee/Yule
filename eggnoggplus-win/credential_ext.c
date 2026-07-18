#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wincred.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "credential_ext.h"

#define CREDENTIAL_EXT_TARGET_PREFIX "EggnoggPlus/YuleOnline/v1/server/"
#define CREDENTIAL_EXT_COMMENT "Eggnogg+ online login"

static size_t credential_ext_bounded_length(const char* value, size_t limit) {
    size_t length = 0;
    if (!value) return 0;
    while (length < limit && value[length] != '\0') length++;
    return length;
}

static void credential_ext_clear_error(char* error, size_t error_cap) {
    if (error && error_cap > 0) error[0] = '\0';
}

static void credential_ext_set_error(char* error, size_t error_cap, const char* format, ...) {
    va_list args;
    if (!error || error_cap == 0) return;
    va_start(args, format);
    vsnprintf(error, error_cap, format, args);
    va_end(args);
    error[error_cap - 1] = '\0';
}

static void credential_ext_set_windows_error(char* error,
                                             size_t error_cap,
                                             const char* operation,
                                             DWORD code) {
    char detail[256];
    DWORD length;
    detail[0] = '\0';
    length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL,
                            code,
                            0,
                            detail,
                            (DWORD)sizeof(detail),
                            NULL);
    while (length > 0 &&
           (detail[length - 1] == '\r' || detail[length - 1] == '\n' ||
            detail[length - 1] == ' ')) {
        detail[--length] = '\0';
    }
    if (length > 0) {
        credential_ext_set_error(error,
                                 error_cap,
                                 "%s failed (Windows error %lu: %s)",
                                 operation,
                                 (unsigned long)code,
                                 detail);
    } else {
        credential_ext_set_error(error,
                                 error_cap,
                                 "%s failed (Windows error %lu)",
                                 operation,
                                 (unsigned long)code);
    }
}

void credential_ext_secure_zero(void* memory, size_t size) {
    if (memory && size > 0) SecureZeroMemory(memory, size);
}

static int credential_ext_is_unreserved(unsigned char byte) {
    return (byte >= 'a' && byte <= 'z') ||
           (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') ||
           byte == '-' || byte == '.' || byte == '_' || byte == '~';
}

static size_t credential_ext_encoded_length(const char* value, size_t length) {
    size_t encoded = 0;
    size_t i;
    for (i = 0; i < length; ++i) {
        encoded += credential_ext_is_unreserved((unsigned char)value[i]) ? 1u : 3u;
    }
    return encoded;
}

static char credential_ext_hex(unsigned int value) {
    static const char digits[] = "0123456789ABCDEF";
    return digits[value & 15u];
}

static char* credential_ext_encode_component(char* output,
                                             const char* value,
                                             size_t length,
                                             int lowercase_ascii) {
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)value[i];
        if (lowercase_ascii && byte >= 'A' && byte <= 'Z') {
            byte = (unsigned char)(byte + ('a' - 'A'));
        }
        if (credential_ext_is_unreserved(byte)) {
            *output++ = (char)byte;
        } else {
            *output++ = '%';
            *output++ = credential_ext_hex(byte >> 4);
            *output++ = credential_ext_hex(byte);
        }
    }
    return output;
}

static CredentialExtResult credential_ext_validate_identity(const char* server,
                                                             unsigned int port,
                                                             const char* username,
                                                             size_t* server_len,
                                                             size_t* username_len,
                                                             char* error,
                                                             size_t error_cap) {
    size_t i;
    *server_len = credential_ext_bounded_length(server, CREDENTIAL_EXT_SERVER_MAX + 1u);
    *username_len = credential_ext_bounded_length(username, CREDENTIAL_EXT_USERNAME_MAX + 1u);

    if (!server || *server_len == 0) {
        credential_ext_set_error(error, error_cap, "Credential server must not be empty");
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    if (*server_len > CREDENTIAL_EXT_SERVER_MAX) {
        credential_ext_set_error(error,
                                 error_cap,
                                 "Credential server exceeds %u bytes",
                                 CREDENTIAL_EXT_SERVER_MAX);
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    if (port == 0 || port > 65535u) {
        credential_ext_set_error(error, error_cap, "Credential port must be between 1 and 65535");
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    if (!username || *username_len == 0) {
        credential_ext_set_error(error, error_cap, "Credential username must not be empty");
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    if (*username_len > CREDENTIAL_EXT_USERNAME_MAX) {
        credential_ext_set_error(error,
                                 error_cap,
                                 "Credential username exceeds %u bytes",
                                 CREDENTIAL_EXT_USERNAME_MAX);
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }

    for (i = 0; i < *server_len; ++i) {
        unsigned char byte = (unsigned char)server[i];
        if (byte <= 0x20u || byte == 0x7fu) {
            credential_ext_set_error(error,
                                     error_cap,
                                     "Credential server contains whitespace or control characters");
            return CREDENTIAL_EXT_INVALID_ARGUMENT;
        }
    }
    for (i = 0; i < *username_len; ++i) {
        unsigned char byte = (unsigned char)username[i];
        if (byte < 0x20u || byte == 0x7fu) {
            credential_ext_set_error(error,
                                     error_cap,
                                     "Credential username contains control characters");
            return CREDENTIAL_EXT_INVALID_ARGUMENT;
        }
    }
    if (username[0] == ' ' || username[*username_len - 1] == ' ') {
        credential_ext_set_error(error,
                                 error_cap,
                                 "Credential username must not start or end with a space");
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    return CREDENTIAL_EXT_OK;
}

CredentialExtResult credential_ext_build_target(const char* server,
                                                 unsigned int port,
                                                 const char* username,
                                                 char* target,
                                                 size_t target_cap,
                                                 size_t* target_len,
                                                 char* error,
                                                 size_t error_cap) {
    static const char port_separator[] = "/port/";
    static const char user_separator[] = "/user/";
    size_t server_len = 0;
    size_t username_len = 0;
    size_t required;
    size_t prefix_len = sizeof(CREDENTIAL_EXT_TARGET_PREFIX) - 1u;
    size_t port_len;
    char port_text[6];
    char* cursor;
    CredentialExtResult result;

    credential_ext_clear_error(error, error_cap);
    if (target_len) *target_len = 0;
    if (target && target_cap > 0) target[0] = '\0';
    if (!target || target_cap == 0) {
        credential_ext_set_error(error, error_cap, "Credential target buffer is invalid");
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }

    result = credential_ext_validate_identity(server,
                                              port,
                                              username,
                                              &server_len,
                                              &username_len,
                                              error,
                                              error_cap);
    if (result != CREDENTIAL_EXT_OK) return result;

    snprintf(port_text, sizeof(port_text), "%u", port);
    port_text[sizeof(port_text) - 1u] = '\0';
    port_len = strlen(port_text);
    required = prefix_len + credential_ext_encoded_length(server, server_len) +
               (sizeof(port_separator) - 1u) + port_len +
               (sizeof(user_separator) - 1u) +
               credential_ext_encoded_length(username, username_len);
    if (target_len) *target_len = required;
    if (required >= target_cap) {
        credential_ext_set_error(error,
                                 error_cap,
                                 "Credential target buffer needs %lu bytes",
                                 (unsigned long)(required + 1u));
        return CREDENTIAL_EXT_BUFFER_TOO_SMALL;
    }

    cursor = target;
    memcpy(cursor, CREDENTIAL_EXT_TARGET_PREFIX, prefix_len);
    cursor += prefix_len;
    cursor = credential_ext_encode_component(cursor, server, server_len, 1);
    memcpy(cursor, port_separator, sizeof(port_separator) - 1u);
    cursor += sizeof(port_separator) - 1u;
    memcpy(cursor, port_text, port_len);
    cursor += port_len;
    memcpy(cursor, user_separator, sizeof(user_separator) - 1u);
    cursor += sizeof(user_separator) - 1u;
    cursor = credential_ext_encode_component(cursor, username, username_len, 0);
    *cursor = '\0';
    return CREDENTIAL_EXT_OK;
}

static CredentialExtResult credential_ext_make_target(const char* server,
                                                       unsigned int port,
                                                       const char* username,
                                                       char target[CREDENTIAL_EXT_TARGET_MAX],
                                                       char* error,
                                                       size_t error_cap) {
    return credential_ext_build_target(server,
                                       port,
                                       username,
                                       target,
                                       CREDENTIAL_EXT_TARGET_MAX,
                                       NULL,
                                       error,
                                       error_cap);
}

CredentialExtResult credential_ext_read_password(const char* server,
                                                  unsigned int port,
                                                  const char* username,
                                                  char* password,
                                                  size_t password_cap,
                                                  size_t* password_len,
                                                  char* error,
                                                  size_t error_cap) {
    char target[CREDENTIAL_EXT_TARGET_MAX];
    PCREDENTIALA stored = NULL;
    CredentialExtResult result;
    size_t length;
    DWORD windows_error;

    credential_ext_clear_error(error, error_cap);
    if (password_len) *password_len = 0;
    if (!password || password_cap == 0) {
        credential_ext_set_error(error, error_cap, "Password output buffer is invalid");
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    credential_ext_secure_zero(password, password_cap);
    result = credential_ext_make_target(server, port, username, target, error, error_cap);
    if (result != CREDENTIAL_EXT_OK) return result;

    if (!CredReadA(target, CRED_TYPE_GENERIC, 0, &stored)) {
        windows_error = GetLastError();
        if (windows_error == ERROR_NOT_FOUND) {
            credential_ext_set_error(error, error_cap, "No saved password exists for this account");
            return CREDENTIAL_EXT_NOT_FOUND;
        }
        credential_ext_set_windows_error(error, error_cap, "Reading saved password", windows_error);
        return CREDENTIAL_EXT_SYSTEM_ERROR;
    }

    length = (size_t)stored->CredentialBlobSize;
    if (length > CREDENTIAL_EXT_PASSWORD_MAX ||
        (length > 0 && !stored->CredentialBlob) ||
        (length > 0 && memchr(stored->CredentialBlob, '\0', length) != NULL)) {
        result = CREDENTIAL_EXT_MALFORMED_CREDENTIAL;
        credential_ext_set_error(error,
                                 error_cap,
                                 "Saved password has an unsupported Credential Manager format");
    } else if (length >= password_cap) {
        result = CREDENTIAL_EXT_BUFFER_TOO_SMALL;
        if (password_len) *password_len = length;
        credential_ext_set_error(error,
                                 error_cap,
                                 "Password output buffer needs %lu bytes",
                                 (unsigned long)(length + 1u));
    } else {
        if (length > 0) memcpy(password, stored->CredentialBlob, length);
        password[length] = '\0';
        if (password_len) *password_len = length;
        result = CREDENTIAL_EXT_OK;
    }

    if (stored->CredentialBlob && stored->CredentialBlobSize > 0) {
        credential_ext_secure_zero(stored->CredentialBlob, stored->CredentialBlobSize);
    }
    CredFree(stored);
    return result;
}

CredentialExtResult credential_ext_write_password(const char* server,
                                                   unsigned int port,
                                                   const char* username,
                                                   const char* password,
                                                   char* error,
                                                   size_t error_cap) {
    char target[CREDENTIAL_EXT_TARGET_MAX];
    unsigned char secret[CREDENTIAL_EXT_PASSWORD_MAX];
    CREDENTIALA credential;
    size_t password_len;
    CredentialExtResult result;
    DWORD windows_error;
    BOOL written;

    credential_ext_clear_error(error, error_cap);
    if (!password) {
        credential_ext_set_error(error, error_cap, "Password must not be null");
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    password_len = credential_ext_bounded_length(password, CREDENTIAL_EXT_PASSWORD_MAX + 1u);
    if (password_len > CREDENTIAL_EXT_PASSWORD_MAX) {
        credential_ext_set_error(error,
                                 error_cap,
                                 "Password exceeds Credential Manager's %u-byte limit",
                                 CREDENTIAL_EXT_PASSWORD_MAX);
        return CREDENTIAL_EXT_SECRET_TOO_LARGE;
    }
    result = credential_ext_make_target(server, port, username, target, error, error_cap);
    if (result != CREDENTIAL_EXT_OK) return result;

    memset(&credential, 0, sizeof(credential));
    memset(secret, 0, sizeof(secret));
    if (password_len > 0) memcpy(secret, password, password_len);
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target;
    credential.Comment = (LPSTR)CREDENTIAL_EXT_COMMENT;
    credential.CredentialBlobSize = (DWORD)password_len;
    credential.CredentialBlob = secret;
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = (LPSTR)username;

    written = CredWriteA(&credential, 0);
    windows_error = written ? ERROR_SUCCESS : GetLastError();
    credential_ext_secure_zero(secret, sizeof(secret));
    credential_ext_secure_zero(&credential, sizeof(credential));
    if (!written) {
        credential_ext_set_windows_error(error, error_cap, "Saving password", windows_error);
        return CREDENTIAL_EXT_SYSTEM_ERROR;
    }
    return CREDENTIAL_EXT_OK;
}

CredentialExtResult credential_ext_delete_password(const char* server,
                                                    unsigned int port,
                                                    const char* username,
                                                    char* error,
                                                    size_t error_cap) {
    char target[CREDENTIAL_EXT_TARGET_MAX];
    CredentialExtResult result;
    DWORD windows_error;

    credential_ext_clear_error(error, error_cap);
    result = credential_ext_make_target(server, port, username, target, error, error_cap);
    if (result != CREDENTIAL_EXT_OK) return result;
    if (CredDeleteA(target, CRED_TYPE_GENERIC, 0)) return CREDENTIAL_EXT_OK;

    windows_error = GetLastError();
    if (windows_error == ERROR_NOT_FOUND) {
        credential_ext_set_error(error, error_cap, "No saved password exists for this account");
        return CREDENTIAL_EXT_NOT_FOUND;
    }
    credential_ext_set_windows_error(error, error_cap, "Deleting saved password", windows_error);
    return CREDENTIAL_EXT_SYSTEM_ERROR;
}

const char* credential_ext_result_name(CredentialExtResult result) {
    switch (result) {
        case CREDENTIAL_EXT_OK: return "ok";
        case CREDENTIAL_EXT_NOT_FOUND: return "not_found";
        case CREDENTIAL_EXT_INVALID_ARGUMENT: return "invalid_argument";
        case CREDENTIAL_EXT_BUFFER_TOO_SMALL: return "buffer_too_small";
        case CREDENTIAL_EXT_SECRET_TOO_LARGE: return "secret_too_large";
        case CREDENTIAL_EXT_MALFORMED_CREDENTIAL: return "malformed_credential";
        case CREDENTIAL_EXT_SYSTEM_ERROR: return "system_error";
        default: return "unknown";
    }
}

