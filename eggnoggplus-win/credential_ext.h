#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Conservative limits shared by all supported Windows Credential Manager
 * versions. Passwords are stored as UTF-8 bytes without a trailing NUL. */
#define CREDENTIAL_EXT_SERVER_MAX 255u
#define CREDENTIAL_EXT_USERNAME_MAX 255u
#define CREDENTIAL_EXT_PASSWORD_MAX 512u
#define CREDENTIAL_EXT_TARGET_MAX 1600u

typedef enum CredentialExtResult {
    CREDENTIAL_EXT_OK = 0,
    CREDENTIAL_EXT_NOT_FOUND,
    CREDENTIAL_EXT_INVALID_ARGUMENT,
    CREDENTIAL_EXT_BUFFER_TOO_SMALL,
    CREDENTIAL_EXT_SECRET_TOO_LARGE,
    CREDENTIAL_EXT_MALFORMED_CREDENTIAL,
    CREDENTIAL_EXT_SYSTEM_ERROR
} CredentialExtResult;

/* Produces the CRED_TYPE_GENERIC target used by the functions below. The
 * server is ASCII case-insensitive; username bytes remain case-sensitive.
 * target_len receives the required byte count excluding the trailing NUL,
 * including when target_cap is too small. */
CredentialExtResult credential_ext_build_target(const char* server,
                                                 unsigned int port,
                                                 const char* username,
                                                 char* target,
                                                 size_t target_cap,
                                                 size_t* target_len,
                                                 char* error,
                                                 size_t error_cap);

/* Reads a password into password and always clears that entire buffer first.
 * password_len receives the password size excluding the trailing NUL. */
CredentialExtResult credential_ext_read_password(const char* server,
                                                  unsigned int port,
                                                  const char* username,
                                                  char* password,
                                                  size_t password_cap,
                                                  size_t* password_len,
                                                  char* error,
                                                  size_t error_cap);

/* Persists a password for the current Windows user on this machine. The
 * caller retains ownership of password and should clear it when no longer
 * needed. */
CredentialExtResult credential_ext_write_password(const char* server,
                                                   unsigned int port,
                                                   const char* username,
                                                   const char* password,
                                                   char* error,
                                                   size_t error_cap);

CredentialExtResult credential_ext_delete_password(const char* server,
                                                    unsigned int port,
                                                    const char* username,
                                                    char* error,
                                                    size_t error_cap);

/* Uses Windows' non-optimizable secure memory clearing primitive. */
void credential_ext_secure_zero(void* memory, size_t size);

const char* credential_ext_result_name(CredentialExtResult result);

#ifdef __cplusplus
}
#endif

