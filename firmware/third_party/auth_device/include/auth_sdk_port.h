#ifndef AUTH_SDK_PORT_H
#define AUTH_SDK_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "auth_device.h"

#define AUTH_SDK_ALGORITHM_P256 "P-256"

typedef struct {
    auth_device_client client;
    uint8_t public_key[65];
    uint8_t private_key[32];
    char endpoint_id[37];
    bool initialized;
} auth_sdk_port_t;

int auth_sdk_port_init(auth_sdk_port_t *port, const char *base_url,
                       const char *activation_code, const char *endpoint_id);
int auth_sdk_port_get_token(auth_sdk_port_t *port, const char **token);
void auth_sdk_port_clear_token(auth_sdk_port_t *port);

/* Override this weak provider with the production Auth CA chain. */
const uint8_t *auth_sdk_port_ca_cert(size_t *length);

#endif
