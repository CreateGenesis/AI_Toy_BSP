/**
 * @file gw_config.h
 * @brief Site settings for the voice Gateway link. Edit and rebuild.
 *
 * The device triple must equal DEVICE_ID, TOKEN and FIRMWARE_VERSION in the
 * Gateway's platform/.env byte for byte, or the Gateway answers AUTH_FAILED.
 */

#ifndef GW_CONFIG_H
#define GW_CONFIG_H

#define GW_WIFI_SSID     "YOLO"
#define GW_WIFI_PASSWORD "88888888"

/* PC running platform/gateway. TCP 8765 only, not 8081 or 3000.
 * Other lab addresses: 192.168.0.200 (Ethernet), 192.168.137.1 (hotspot). */
#define GW_SERVER_IP     "192.168.31.235"
#define GW_SERVER_PORT   8765

#define GW_DEVICE_ID     "t5-test-001"
#define GW_TOKEN         "demo-token-12345"
#define GW_FIRMWARE      "1.0.1-test"

/* Speaker volume 0-100. The verified tone test uses 25. */
#define GW_VOLUME        50

#endif
