/**
 * @file ws2812.c
 * @brief Bit-banged driver for the two WS2812B-2020-V6 pixels on GPIO9.
 *
 * The SDK WS2812 driver needs an SPI MOSI pin, and T5AI SPI MOSI maps only to
 * GPIO16/35/46 (SPI0) or GPIO4 (SPI1), so Light_DO on GPIO9 is driven
 * directly. Each edge is one store to the GPIO9 config register (bit 1 is the
 * output level) followed by DSB, timed with the Cortex-M33 DWT cycle counter.
 *
 * WS2812B-2020-V6 datasheet: T0H 220-380 ns, T1H 580-1000 ns, T0L/T1L
 * 580-1000 ns, bit period >= 1.25 us, reset low >= 280 us, 24 bits per pixel
 * in G, R, B order, MSB first.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_gpio.h"
#include "tkl_system.h"

#include "ws2812.h"

#define WS2812_PIN          TUYA_GPIO_NUM_9
/* AP core runs secure (CONFIG_SPE): SOC_AON_GPIO_REG_BASE 0x44000400,
 * one 32-bit config register per GPIO. */
#define WS2812_GPIO_REG     ((volatile uint32_t *)(0x44000400u + 9u * 4u))
#define WS2812_GPIO_OUT_BIT (1u << 1)

#define DWT_DEMCR           (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL            (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT          (*(volatile uint32_t *)0xE0001004u)

#define WS2812_T0H_NS       300
#define WS2812_T0L_NS       950
#define WS2812_T1H_NS       700
#define WS2812_T1L_NS       600
#define WS2812_RESET_MS     1 /* > 280 us */
#define WS2812_HOLD_MS      1000

static uint32_t s_t0h, s_t0l, s_t1h, s_t1l;
static uint32_t s_out_hi, s_out_lo;
static bool s_ready = false;
static CHAT_ROBOT_RGB_T s_hold[CHAT_ROBOT_WS2812_COUNT];
static THREAD_HANDLE s_hold_thread = NULL;

/* Busy-waits until `cycles` have elapsed since `start`. Runs with interrupts
 * off, so it must always end: each pass costs at least one cycle, so after
 * `cycles` passes the time is up or the counter has stopped. */
static inline bool ws2812_wait_until(uint32_t start, uint32_t cycles)
{
    uint32_t guard = cycles;

    while ((DWT_CYCCNT - start) < cycles) {
        if (0 == guard--) {
            return false;
        }
    }
    return true;
}

/* Measures the CPU clock against the millisecond tick over 50 ms. */
static uint32_t ws2812_measure_hz(void)
{
    SYS_TIME_T t0, t1;
    uint32_t c0, c1;

    t0 = tal_system_get_millisecond();
    while (tal_system_get_millisecond() == t0) {
    }
    c0 = DWT_CYCCNT;
    t1 = tal_system_get_millisecond();
    while (tal_system_get_millisecond() - t1 < 50) {
    }
    c1 = DWT_CYCCNT;
    return (c1 - c0) * 20u;
}

OPERATE_RET chat_robot_ws2812_init(void)
{
    TUYA_GPIO_BASE_CFG_T cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW,
    };
    OPERATE_RET rt = OPRT_OK;
    uint32_t hz, reg, c0;

    TUYA_CALL_ERR_RETURN(tkl_gpio_init(WS2812_PIN, &cfg));
    reg = *WS2812_GPIO_REG;
    s_out_hi = reg | WS2812_GPIO_OUT_BIT;
    s_out_lo = reg & ~WS2812_GPIO_OUT_BIT;

    DWT_DEMCR |= (1u << 24); /* TRCENA */
    DWT_CTRL |= 1u;          /* CYCCNTENA */
    c0 = DWT_CYCCNT;
    tal_system_sleep(1);
    if (DWT_CYCCNT == c0) {
        PR_ERR("WS2812: DWT cycle counter not running, LEDs disabled");
        return OPRT_NOT_SUPPORTED;
    }

    hz = ws2812_measure_hz();
    s_t0h = (uint32_t)((uint64_t)hz * WS2812_T0H_NS / 1000000000u);
    s_t0l = (uint32_t)((uint64_t)hz * WS2812_T0L_NS / 1000000000u);
    s_t1h = (uint32_t)((uint64_t)hz * WS2812_T1H_NS / 1000000000u);
    s_t1l = (uint32_t)((uint64_t)hz * WS2812_T1L_NS / 1000000000u);
    PR_NOTICE("WS2812 on GPIO9: cpu %u Hz, reg 0x%08x, T0H %u T1H %u cycles", hz, reg, s_t0h, s_t1h);
    s_ready = true;
    return rt;
}

OPERATE_RET chat_robot_ws2812_show(const CHAT_ROBOT_RGB_T colors[CHAT_ROBOT_WS2812_COUNT])
{
    uint8_t bytes[CHAT_ROBOT_WS2812_COUNT * 3];
    uint32_t irq, i;
    bool ok = true;

    if (!s_ready) {
        return OPRT_RESOURCE_NOT_READY;
    }
    for (i = 0; i < CHAT_ROBOT_WS2812_COUNT; i++) {
        bytes[i * 3] = colors[i].g;
        bytes[i * 3 + 1] = colors[i].r;
        bytes[i * 3 + 2] = colors[i].b;
    }

    /* Other code may reconfigure the debug unit; re-arm the counter. */
    DWT_DEMCR |= (1u << 24);
    DWT_CTRL |= 1u;

    irq = tkl_system_enter_critical();
    for (i = 0; i < sizeof(bytes) && ok; i++) {
        uint8_t v = bytes[i];
        int b;

        for (b = 7; b >= 0 && ok; b--) {
            bool one = (v >> b) & 1u;
            uint32_t t = DWT_CYCCNT;

            *WS2812_GPIO_REG = s_out_hi;
            __asm volatile("dsb" ::: "memory");
            ok = ws2812_wait_until(t, one ? s_t1h : s_t0h);
            t = DWT_CYCCNT;
            *WS2812_GPIO_REG = s_out_lo;
            __asm volatile("dsb" ::: "memory");
            ok = ok && ws2812_wait_until(t, one ? s_t1l : s_t0l);
        }
    }
    *WS2812_GPIO_REG = s_out_lo;
    tkl_system_exit_critical(irq);
    tal_system_sleep(WS2812_RESET_MS);
    if (!ok) {
        PR_ERR("WS2812: cycle counter stalled, frame aborted");
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

static void ws2812_hold_thread(void *arg)
{
    (void)arg;

    while (1) {
        chat_robot_ws2812_show(s_hold);
        tal_system_sleep(WS2812_HOLD_MS);
    }
}

OPERATE_RET chat_robot_ws2812_hold(const CHAT_ROBOT_RGB_T colors[CHAT_ROBOT_WS2812_COUNT])
{
    THREAD_CFG_T cfg = {
        .stackDepth = 2048,
        .priority = THREAD_PRIO_3,
        .thrdname = "ws2812",
    };
    OPERATE_RET rt = OPRT_OK;

    memcpy(s_hold, colors, sizeof(s_hold));
    /* The first frame may run from cold cache; the second one is exact. */
    TUYA_CALL_ERR_RETURN(chat_robot_ws2812_show(s_hold));
    TUYA_CALL_ERR_RETURN(chat_robot_ws2812_show(s_hold));
    if (NULL == s_hold_thread) {
        rt = tal_thread_create_and_start(&s_hold_thread, NULL, NULL, ws2812_hold_thread, NULL, &cfg);
    }
    return rt;
}
