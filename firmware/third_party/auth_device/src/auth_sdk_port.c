#include "auth_sdk_port.h"

#include <string.h>

#include "http_client_interface.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "tal_api.h"
#include "tal_kv.h"
#include "tkl_system.h"

#define AUTH_KV_PRIVATE_KEY "auth.private_key"
#define AUTH_KV_PUBLIC_KEY "auth.public_key"
#define AUTH_KV_DEVICE_ID "auth.device_id"
#define AUTH_KV_ACTIVATION_CODE "auth.activation_code"

static int kv_read(const char *key, uint8_t *out, size_t capacity, size_t *length)
{
    uint8_t *value = NULL;
    size_t value_length = 0;
    int result;

    if (!key || !out || !length) {
        return -1;
    }
    result = tal_kv_get(key, &value, &value_length);
    if (result != 0) {
        return 1;
    }
    if (!value || value_length > capacity) {
        tal_kv_free(value);
        return -1;
    }
    memcpy(out, value, value_length);
    *length = value_length;
    tal_kv_free(value);
    return 0;
}

static int kv_write(const char *key, const uint8_t *value, size_t length)
{
    return (key && value && length <= UINT16_MAX && tal_kv_set(key, value, length) == 0) ? 0 : -1;
}

static int auth_rng(void *context, unsigned char *output, size_t length)
{
    size_t offset = 0;
    (void)context;

    while (offset < length) {
        uint32_t word = (uint32_t)tkl_system_get_random(UINT32_MAX);
        size_t copy = length - offset;
        if (copy > sizeof(word)) {
            copy = sizeof(word);
        }
        memcpy(output + offset, &word, copy);
        offset += copy;
    }
    return 0;
}

static int make_p256_key(uint8_t private_key[32], uint8_t public_key[65])
{
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi private_scalar;
    int result;

    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&private_scalar);

    result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0) {
        result = mbedtls_ecp_gen_keypair(&group, &private_scalar, &point, auth_rng, NULL);
    }
    if (result == 0) {
        result = mbedtls_mpi_write_binary(&private_scalar, private_key, 32);
    }
    if (result == 0) {
        size_t olen = 0;
        result = mbedtls_ecp_point_write_binary(&group, &point, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                &olen, public_key, 65);
        if (result == 0 && olen != 65) {
            result = -1;
        }
    }

    mbedtls_mpi_free(&private_scalar);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return result;
}

static int auth_sign(void *context, const uint8_t *message, size_t length, uint8_t signature[64])
{
    auth_sdk_port_t *port = (auth_sdk_port_t *)context;
    mbedtls_ecp_group group;
    mbedtls_mpi private_scalar;
    mbedtls_mpi r;
    mbedtls_mpi s;
    uint8_t digest[32];
    int result;

    if (!port || !message || !signature || !port->initialized) {
        return -1;
    }

    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&private_scalar);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    result = mbedtls_sha256(message, length, digest, 0);
    if (result == 0) {
        result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    }
    if (result == 0) {
        result = mbedtls_mpi_read_binary(&private_scalar, port->private_key,
                                         sizeof(port->private_key));
    }
    if (result == 0) {
        result = mbedtls_ecdsa_sign(&group, &r, &s, &private_scalar,
                                    digest, sizeof(digest), auth_rng, NULL);
    }
    if (result == 0) {
        result = mbedtls_mpi_write_binary(&r, signature, 32);
    }
    if (result == 0) {
        result = mbedtls_mpi_write_binary(&s, signature + 32, 32);
    }

    memset(digest, 0, sizeof(digest));
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&private_scalar);
    mbedtls_ecp_group_free(&group);
    return result;
}

static int auth_load_identity(void *context, char device_id[37])
{
    size_t length = 0;
    int result;
    (void)context;

    result = kv_read(AUTH_KV_DEVICE_ID, (uint8_t *)device_id, 37, &length);
    if (result != 0) {
        return result > 0 ? 1 : -1;
    }
    if (length != 36) {
        memset(device_id, 0, 37);
        return -1;
    }
    device_id[36] = '\0';
    return 0;
}

static int auth_save_identity(void *context, const char device_id[37])
{
    (void)context;
    if (!device_id || strlen(device_id) != 36) {
        return -1;
    }
    return kv_write(AUTH_KV_DEVICE_ID, (const uint8_t *)device_id, 36);
}

static int auth_clear_code(void *context)
{
    (void)context;
    return tal_kv_del(AUTH_KV_ACTIVATION_CODE) == 0 ? 0 : -1;
}

static uint64_t auth_monotonic_ms(void *context)
{
    (void)context;
    return (uint64_t)tal_system_get_millisecond();
}

static int parse_base_url(const char *base_url, char *host, size_t host_capacity,
                          uint16_t *port)
{
    const char *start;
    const char *end;
    const char *colon = NULL;
    size_t length;

    if (!base_url || !host || !port || strncmp(base_url, "https://", 8) != 0) {
        return -1;
    }
    start = base_url + 8;
    end = strchr(start, '/');
    if (!end) {
        end = start + strlen(start);
    }
    for (const char *cursor = start; cursor < end; cursor++) {
        if (*cursor == ':') {
            colon = cursor;
            break;
        }
    }
    length = (size_t)((colon ? colon : end) - start);
    if (length == 0 || length >= host_capacity) {
        return -1;
    }
    memcpy(host, start, length);
    host[length] = '\0';
    *port = 443;
    if (colon) {
        unsigned value = 0;
        for (const char *cursor = colon + 1; cursor < end; cursor++) {
            if (*cursor < '0' || *cursor > '9') {
                return -1;
            }
            value = value * 10U + (unsigned)(*cursor - '0');
            if (value > UINT16_MAX) {
                return -1;
            }
        }
        *port = (uint16_t)value;
    }
    return 0;
}

static int auth_http(void *context, const char *base_url, const char *path,
                     const char *idempotency_key, const char *body, char *response,
                     size_t response_capacity, size_t *response_length, int *status)
{
    char host[128];
    uint16_t port = 0;
    http_client_header_t headers[3];
    http_client_request_t request;
    http_client_response_t http_response;
    size_t ca_length = 0;
    const uint8_t *ca = auth_sdk_port_ca_cert(&ca_length);
    int result;

    (void)context;
    /* TuyaOpen selects plain TCP when cacert is NULL. Refuse that fallback:
     * Auth Device SDK requires HTTPS with certificate verification. */
    if (!ca || ca_length == 0) {
        return -1;
    }
    if (!path || !body || !response || !response_length || !status ||
        parse_base_url(base_url, host, sizeof(host), &port) != 0) {
        return -1;
    }

    headers[0] = (http_client_header_t){"Content-Type", "application/json"};
    headers[1] = (http_client_header_t){"Accept", "application/json"};
    headers[2] = (http_client_header_t){"Idempotency-Key", idempotency_key ? idempotency_key : ""};
    memset(&request, 0, sizeof(request));
    request.host = host;
    request.port = port;
    request.path = path;
    request.cacert = ca;
    request.cacert_len = ca_length;
    request.tls_no_verify = false;
    request.method = "POST";
    request.headers = headers;
    request.headers_count = idempotency_key ? 3 : 2;
    request.body = (const uint8_t *)body;
    request.body_length = strlen(body);
    request.timeout_ms = 15000;

    memset(&http_response, 0, sizeof(http_response));
    http_response.buffer = (uint8_t *)response;
    http_response.buffer_length = response_capacity;
    result = http_client_request(&request, &http_response);
    if (result != HTTP_CLIENT_SUCCESS || http_response.body_length > response_capacity ||
        (http_response.body_length > 0 && !http_response.body)) {
        http_client_free(&http_response);
        return -1;
    }
    *status = http_response.status_code;
    if (http_response.body_length > 0) {
        memmove(response, http_response.body, http_response.body_length);
    }
    *response_length = http_response.body_length;
    http_client_free(&http_response);
    return 0;
}

__attribute__((weak)) const uint8_t *auth_sdk_port_ca_cert(size_t *length)
{
    if (length) {
        *length = 0;
    }
    return NULL;
}

static auth_device_platform make_platform(auth_sdk_port_t *port)
{
    auth_device_platform platform;
    memset(&platform, 0, sizeof(platform));
    platform.context = port;
    platform.http = auth_http;
    platform.random = auth_rng;
    platform.monotonic_ms = auth_monotonic_ms;
    platform.sign = auth_sign;
    platform.load_identity = auth_load_identity;
    platform.save_identity = auth_save_identity;
    platform.clear_code = auth_clear_code;
    return platform;
}

int auth_sdk_port_init(auth_sdk_port_t *port, const char *base_url,
                       const char *activation_code, const char *endpoint_id)
{
    auth_device_platform platform;
    char code[AUTH_DEVICE_CODE_CAP];
    const char *code_arg = activation_code;
    size_t private_length = 0;
    size_t public_length = 0;
    size_t code_length = 0;
    int result;

    if (!port || !base_url || !endpoint_id || strlen(endpoint_id) != 36) {
        return AUTH_DEVICE_ARGUMENT;
    }
    memset(port, 0, sizeof(*port));
    strncpy(port->endpoint_id, endpoint_id, sizeof(port->endpoint_id) - 1);

    result = kv_read(AUTH_KV_PRIVATE_KEY, port->private_key, sizeof(port->private_key), &private_length);
    if (result == 1) {
        result = make_p256_key(port->private_key, port->public_key);
        if (result == 0) {
            result = kv_write(AUTH_KV_PRIVATE_KEY, port->private_key, sizeof(port->private_key));
        }
        if (result == 0) {
            result = kv_write(AUTH_KV_PUBLIC_KEY, port->public_key, sizeof(port->public_key));
        }
    } else if (result == 0) {
        if (private_length != sizeof(port->private_key)) {
            return AUTH_DEVICE_STORAGE;
        }
        result = kv_read(AUTH_KV_PUBLIC_KEY, port->public_key, sizeof(port->public_key), &public_length);
        if (result != 0 || public_length != sizeof(port->public_key)) {
            return AUTH_DEVICE_STORAGE;
        }
    }
    if (result != 0) {
        return AUTH_DEVICE_STORAGE;
    }

    /* Factory provisioning stores the one-time code in KV. A caller may
     * still provide an explicit code for a controlled manufacturing flow. */
    memset(code, 0, sizeof(code));
    if (!code_arg || !*code_arg) {
        result = kv_read(AUTH_KV_ACTIVATION_CODE, (uint8_t *)code,
                         sizeof(code) - 1, &code_length);
        if (result < 0) {
            return AUTH_DEVICE_STORAGE;
        }
        if (result == 0) {
            code[code_length] = '\0';
            code_arg = code;
        } else {
            code_arg = NULL;
        }
    }

    port->initialized = true;
    platform = make_platform(port);
    result = auth_device_init(&port->client, &platform, base_url, AUTH_SDK_ALGORITHM_P256,
                              port->public_key, sizeof(port->public_key), code_arg);
    memset(code, 0, sizeof(code));
    if (result != AUTH_DEVICE_OK) {
        port->initialized = false;
    }
    return result;
}

int auth_sdk_port_get_token(auth_sdk_port_t *port, const char **token)
{
    if (!port || !port->initialized || !token) {
        return AUTH_DEVICE_ARGUMENT;
    }
    return auth_device_get_token(&port->client, port->endpoint_id, token);
}

void auth_sdk_port_clear_token(auth_sdk_port_t *port)
{
    if (port) {
        auth_device_clear_token(&port->client);
    }
}
