#ifndef CHAT_ROBOT_WS2812_H
#define CHAT_ROBOT_WS2812_H

#include "tuya_cloud_types.h"

/* Two WS2812B-2020-V6 pixels chained on Light_DO (GPIO9). */
#define CHAT_ROBOT_WS2812_COUNT 2

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} CHAT_ROBOT_RGB_T;

/* Configures GPIO9 and calibrates the bit timing against the system clock. */
OPERATE_RET chat_robot_ws2812_init(void);

/* Sends one frame, pixel 0 first (nearest to GPIO9). */
OPERATE_RET chat_robot_ws2812_show(const CHAT_ROBOT_RGB_T colors[CHAT_ROBOT_WS2812_COUNT]);

/* Shows the colors and re-sends them every second from a small thread, so
 * a frame disturbed by bus traffic is corrected without visible flicker. */
OPERATE_RET chat_robot_ws2812_hold(const CHAT_ROBOT_RGB_T colors[CHAT_ROBOT_WS2812_COUNT]);

#endif
