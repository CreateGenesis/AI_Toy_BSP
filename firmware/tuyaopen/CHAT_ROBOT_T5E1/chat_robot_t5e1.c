/**
 * @file chat_robot_t5e1.c
 * @brief Initial board registration for the custom Chat Robot T5-E1 PCB.
 *
 * The PCB has an AW87390FCR amplifier with no separate enable GPIO. It is
 * initialized over software I2C on GPIO38/39 by aw87390.c.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tdd_audio.h"
#include "tdd_button_gpio.h"

#include "aw87390.h"
#include "ws2812.h"

#define CHAT_ROBOT_KEY_PIN       TUYA_GPIO_NUM_3
#define CHAT_ROBOT_SPK_PIN_NONE  TUYA_GPIO_NUM_MAX

OPERATE_RET __board_register_audio(void)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(AUDIO_CODEC_NAME)
    TDD_AUDIO_T5AI_T cfg = {0};

#if defined(CHAT_ROBOT_T5E1_AEC) && (CHAT_ROBOT_T5E1_AEC == 1)
    /* Two ADC channels: MIC1 and the MICP2/MICN2 loopback reference. */
    cfg.aec_enable = 1;
#else
    /* Verified playback path: single microphone channel, no AEC. */
    cfg.aec_enable = 0;
#endif
    cfg.ai_chn = TKL_AI_0;
    cfg.sample_rate = TKL_AUDIO_SAMPLE_16K;
    cfg.data_bits = TKL_AUDIO_DATABITS_16;
    cfg.channel = TKL_AUDIO_CHANNEL_MONO;
    cfg.spk_sample_rate = TKL_AUDIO_SAMPLE_16K;
    cfg.spk_pin = CHAT_ROBOT_SPK_PIN_NONE;
    cfg.spk_pin_polarity = TUYA_GPIO_LEVEL_LOW;

    TUYA_CALL_ERR_RETURN(tdd_audio_register(AUDIO_CODEC_NAME, cfg));
#endif

    return rt;
}

static OPERATE_RET __board_register_button(void)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(BUTTON_NAME)
    /* NET_KEY pulls GPIO3 low. Scan mode with the internal pull-up: the IRQ
     * mode of tdd_button_gpio.c enables a pull-down for active-low keys. */
    BUTTON_GPIO_CFG_T button_cfg = {
        .pin = CHAT_ROBOT_KEY_PIN,
        .level = TUYA_GPIO_LEVEL_LOW,
        .mode = BUTTON_TIMER_SCAN_MODE,
        .pin_type.gpio_pull = TUYA_GPIO_PULLUP,
    };

    TUYA_CALL_ERR_RETURN(tdd_gpio_button_register(BUTTON_NAME, &button_cfg));
#endif

    return rt;
}

OPERATE_RET board_register_hardware(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_LOG(chat_robot_aw87390_init());
    TUYA_CALL_ERR_LOG(__board_register_audio());
    TUYA_CALL_ERR_LOG(__board_register_button());
    /* GPIO9 drives the two chained WS2812B-2020 pixels, not a plain LED. */
    TUYA_CALL_ERR_LOG(chat_robot_ws2812_init());

    return rt;
}
