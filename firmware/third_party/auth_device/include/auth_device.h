#ifndef AUTH_DEVICE_H
#define AUTH_DEVICE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AUTH_DEVICE_TOKEN_CAP 4096
#define AUTH_DEVICE_RESPONSE_CAP 8192
#define AUTH_DEVICE_CODE_CAP 129
typedef enum {
  AUTH_DEVICE_OK = 0,
  AUTH_DEVICE_ARGUMENT = -1,
  AUTH_DEVICE_BUFFER = -2,
  AUTH_DEVICE_NETWORK = -3,
  AUTH_DEVICE_PROTOCOL = -4,
  AUTH_DEVICE_SIGN = -5,
  AUTH_DEVICE_STORAGE = -6,
  AUTH_DEVICE_DENIED = -7,
  AUTH_DEVICE_REVOKED = -8,
  AUTH_DEVICE_RATE_LIMIT = -9,
  AUTH_DEVICE_DEPENDENCY = -10
} auth_device_error;
/* Callbacks must not log request/response bodies, keys or tokens. HTTP must
 * verify the server certificate and hostname. A signer may use a key handle. */
typedef struct {
  void *context;
  int (*http)(void *, const char *base_url, const char *path,
              const char *idempotency_key, const char *body, char *response,
              size_t response_cap, size_t *response_len, int *status);
  int (*random)(void *, uint8_t *output, size_t length);
  uint64_t (*monotonic_ms)(void *);
  int (*sign)(void *, const uint8_t *message, size_t length,
              uint8_t signature[64]);
  /* load_identity: 0=loaded, 1=not provisioned, negative=storage failure. */
  int (*load_identity)(void *, char device_id[37]);
  int (*save_identity)(void *, const char device_id[37]);
  int (*clear_code)(void *);
} auth_device_platform;
typedef struct {
  auth_device_platform platform;
  char base_url[256], algorithm[16], public_key[88], code[AUTH_DEVICE_CODE_CAP];
  char device_id[37], endpoint_id[37], token[AUTH_DEVICE_TOKEN_CAP];
  char supplier_id[37], sn[161];
  uint64_t valid_until_ms;
  char challenge_id[37], nonce[88], request_key[33], signed_body[192];
  uint64_t challenge_started_ms;
  int pending_token;
} auth_device_client;
int auth_device_init(auth_device_client *, const auth_device_platform *,
                     const char *base_url, const char *algorithm,
                     const uint8_t *public_key, size_t key_length,
                     const char *code);
/* SN and supplier are public provisioning metadata; AKs are station-only.
 * SN accepts printable ASCII (1-160 bytes, no whitespace); JSON escaping is
 * internal. Saved identity recovery subsequently needs only the public key. */
int auth_device_init_sn(auth_device_client *, const auth_device_platform *,
                        const char *base_url, const char *algorithm,
                        const uint8_t *public_key, size_t key_length,
                        const char *supplier_id, const char *sn);
/* Token points into client storage; don't free or persist it. On transport
 * uncertainty call again with the same endpoint/client to recover the request.
 */
int auth_device_get_token(auth_device_client *, const char *endpoint_id,
                          const char **token);
void auth_device_clear_token(auth_device_client *);
/* Protocol helpers are shared by SDK conformance tests and hardware signers. */
int auth_device_message(const char *challenge_id, const char *nonce,
                        char *output, size_t capacity);
int auth_device_base64url(const uint8_t *, size_t, char *, size_t);
#ifdef __cplusplus
}
#endif
#endif
