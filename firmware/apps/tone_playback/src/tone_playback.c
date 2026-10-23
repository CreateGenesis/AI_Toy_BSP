/**
 * @file tone_playback.c
 * @brief Loop a fixed 1 kHz PCM tone through the schematic audio path.
 *
 * Playback uses the registered T5 audio driver: AUDIO_LP/LN into the
 * AW87390FCR, then SP+/SP-. The tone is generated at runtime so the image
 * does not carry a waveform file.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"

#include "board_com_api.h"
#include "tdl_audio_manage.h"
#include "aw87390.h"

#define TONE_SAMPLE_RATE_HZ 16000
#define TONE_FREQ_HZ        1000
#define TONE_AMPLITUDE      4000
#define TONE_FRAME_SAMPLES  320
#define TONE_VOLUME         25

static TDL_AUDIO_HANDLE_T s_audio = NULL;
static int16_t s_frame[TONE_FRAME_SAMPLES];
static uint32_t s_phase = 0;

static void tone_mic_discard(TDL_AUDIO_FRAME_FORMAT_E type, TDL_AUDIO_STATUS_E status,
                             uint8_t *data, uint32_t len)
{
    (void)type;
    (void)status;
    (void)data;
    (void)len;
}

static OPERATE_RET tone_audio_open(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tdl_audio_find(AUDIO_CODEC_NAME, &s_audio));
    TUYA_CALL_ERR_RETURN(tdl_audio_open(s_audio, tone_mic_discard));
    TUYA_CALL_ERR_RETURN(tdl_audio_volume_set(s_audio, TONE_VOLUME));
    return rt;
}

static void user_main(void)
{
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    bk_printf("tone app entered\r\n");
    PR_NOTICE("Chat Robot T5-E1 fixed-tone playback, log UART1 460800 8N1");
    PR_NOTICE("Path: T5 AUDIO_LP/LN -> AW87390FCR -> speaker");
    PR_NOTICE("Tone: %u Hz square, %u Hz 16-bit mono, volume %u",
              TONE_FREQ_HZ, TONE_SAMPLE_RATE_HZ, TONE_VOLUME);

    if (OPRT_OK != board_register_hardware()) {
        PR_ERR("board_register_hardware failed; AW87390 I2C on GPIO38/39");
        PR_ERR("continuing so the T5 DAC still drives AUDIO_LP/LN");
    }

    if (OPRT_OK != tone_audio_open()) {
        PR_ERR("audio open failed");
        while (1) {
            tal_system_sleep(1000);
        }
    }

    PR_NOTICE("playing fixed tone, quarter volume");
    while (1) {
        uint32_t i;

        for (i = 0; i < TONE_FRAME_SAMPLES; i++) {
            s_frame[i] = ((s_phase % 16) < 8) ? TONE_AMPLITUDE : -TONE_AMPLITUDE;
            s_phase++;
        }
        if (OPRT_OK != tdl_audio_play(s_audio, (uint8_t *)s_frame, sizeof(s_frame))) {
            PR_ERR("tdl_audio_play failed");
            tal_system_sleep(20);
            continue;
        }
        tal_system_sleep(20);
    }
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    user_main();
}
#else

static THREAD_HANDLE ty_app_thread = NULL;

static void tuya_app_thread(void *arg)
{
    (void)arg;
    user_main();
    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {
        .stackDepth = 8192,
        .priority = THREAD_PRIO_2,
        .thrdname = "tuya_app_main",
    };
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
