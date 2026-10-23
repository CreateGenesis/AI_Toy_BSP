/**
 * @file aec_loopback.c
 * @brief Push-to-talk record/playback microphone and AEC test for the
 *        Chat Robot T5-E1.
 *
 * Hold the NET_KEY (GPIO3) to record, release it to hear the recording.
 *
 * With AEC enabled the T5 ADC runs two channels: MIC1 (main microphone) and
 * MIC2 (MICP2_LB/MICN2_LB, the filtered SP+/SP- reference). The vendor AEC
 * task passes each 20 ms frame of both channels to the hook registered with
 * tkl_ai_set_vad_aec_algorithm(). While the hook is installed it replaces the
 * vendor AEC and the raw microphone is recorded.
 *
 * During each playback the mic also captures the speaker echo, raw on odd
 * playbacks and through the vendor AEC on even ones, and the levels go to the
 * log UART (UART1, 460800 8N1).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_audio.h"
#include "tkl_output.h"

#include "board_com_api.h"
#include "tdl_audio_manage.h"
#include "tdl_button_manage.h"

#define LB_SAMPLE_HZ      16000
#define LB_FRAME_MS       20
#define LB_FRAME_SAMPLES  (LB_SAMPLE_HZ * LB_FRAME_MS / 1000)
#define LB_FRAME_BYTES    (LB_FRAME_SAMPLES * 2)
/* Half volume. The verified tone test uses 25. */
#define LB_VOLUME         50
#define LB_MAX_SECONDS    10
#define LB_MAX_SAMPLES    (LB_MAX_SECONDS * LB_SAMPLE_HZ)
/* Shorter presses are treated as accidental. */
#define LB_MIN_RECORD_MS  300

extern void bk_printf(const char *fmt, ...);
/* tkl_audio.c: the vendor VAD stops the mic output after silence. */
extern void tkl_ai_disable_vendor_vad(void);

static TDL_AUDIO_HANDLE_T s_audio = NULL;
static int16_t *s_voice_mic = NULL;
static int16_t *s_voice_ref = NULL;
static int16_t *s_echo = NULL;
static int16_t *s_echo_ref = NULL;

static int16_t *s_rec_mic = NULL;
static int16_t *s_rec_ref = NULL;
static int16_t *s_rec_out = NULL;
static uint32_t s_rec_cap = 0;
static volatile uint32_t s_rec_raw_n = 0;
static volatile uint32_t s_rec_out_n = 0;
static volatile bool s_raw_armed = false;
static volatile bool s_out_armed = false;
static volatile uint32_t s_hook_frames = 0;
static volatile uint32_t s_cb_frames = 0;
static volatile uint32_t s_cb_last_len = 0;
static volatile bool s_key_down = false;

/* Runs in the vendor AEC task in place of aec_proc(): pass the microphone
 * through unchanged and copy the aligned mic/ref pair while armed. */
static int lb_bypass_hook(short *mic, short *ref, short *out)
{
    memcpy(out, mic, LB_FRAME_BYTES);
    s_hook_frames++;

    if (s_raw_armed) {
        uint32_t n = s_rec_raw_n;
        if (n + LB_FRAME_SAMPLES <= s_rec_cap) {
            memcpy(&s_rec_mic[n], mic, LB_FRAME_BYTES);
            memcpy(&s_rec_ref[n], ref, LB_FRAME_BYTES);
            s_rec_raw_n = n + LB_FRAME_SAMPLES;
        }
    }
    return 0;
}

static void lb_mic_cb(TDL_AUDIO_FRAME_FORMAT_E type, TDL_AUDIO_STATUS_E status, uint8_t *data, uint32_t len)
{
    (void)status;

    s_cb_frames++;
    s_cb_last_len = len;

    if (s_out_armed && TDL_AUDIO_FRAME_FORMAT_PCM == type) {
        uint32_t n = s_rec_out_n;
        uint32_t count = len / 2;
        if (n + count > s_rec_cap) {
            count = s_rec_cap - n;
        }
        memcpy(&s_rec_out[n], data, count * 2);
        s_rec_out_n = n + count;
    }
}

static void lb_key_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E event, void *arg)
{
    (void)name;
    (void)arg;

    s_key_down = (TDL_BUTTON_PRESS_DOWN == event);
}

static void lb_format_db(char *buf, size_t size, float db)
{
    int tenths = (int)lrintf(db * 10.0f);
    const char *sign = (tenths < 0) ? "-" : "";

    if (tenths < 0) {
        tenths = -tenths;
    }
    snprintf(buf, size, "%s%d.%d", sign, tenths / 10, tenths % 10);
}

/* Logs AC level, peak, clipped samples and DC; returns the level in dBFS. */
static float lb_log_stats(const char *name, const int16_t *pcm, uint32_t n)
{
    int64_t sum = 0;
    uint64_t sumsq = 0;
    int32_t peak = 0;
    uint32_t clip = 0;
    uint32_t i;
    float mean, var, rms, level;
    char db[16];

    if (0 == n) {
        bk_printf("@LB-STAT %s n=0 (no audio captured)\r\n", name);
        return -120.0f;
    }
    for (i = 0; i < n; i++) {
        int32_t v = pcm[i];
        int32_t a = (v < 0) ? -v : v;

        sum += v;
        sumsq += (uint64_t)((int64_t)v * v);
        if (a > peak) {
            peak = a;
        }
        if (v >= 32767 || v <= -32768) {
            clip++;
        }
    }
    mean = (float)sum / n;
    var = (float)sumsq / n - mean * mean;
    rms = sqrtf(var > 0.0f ? var : 0.0f);
    level = 20.0f * log10f((rms > 0.5f ? rms : 0.5f) / 32768.0f);
    lb_format_db(db, sizeof(db), level);
    bk_printf("@LB-STAT %s n=%u rms=%sdBFS peak=%d clip=%u dc=%d\r\n", name, n, db, peak, clip, (int)lrintf(mean));
    return level;
}

static void lb_arm(bool aec, int16_t *mic, int16_t *ref, uint32_t cap)
{
    tkl_ai_set_vad_aec_algorithm(aec ? NULL : lb_bypass_hook);
    s_rec_mic = mic;
    s_rec_ref = ref;
    s_rec_out = mic;
    s_rec_cap = cap;
    s_rec_raw_n = 0;
    s_rec_out_n = 0;
    s_raw_armed = !aec;
    s_out_armed = aec;
}

static uint32_t lb_disarm(void)
{
    bool aec = s_out_armed;

    s_raw_armed = false;
    s_out_armed = false;
    return aec ? s_rec_out_n : s_rec_raw_n;
}

/* One frame per 20 ms on a fixed schedule. Sleeping after every frame lets
 * the speaker task (priority 5) drain the DAC. */
static void lb_play(const int16_t *src, uint32_t samples)
{
    uint32_t frames = samples / LB_FRAME_SAMPLES;
    SYS_TIME_T start = tal_system_get_millisecond();
    uint32_t errors = 0;
    uint32_t k;

    for (k = 0; k < frames; k++) {
        SYS_TIME_T due, now;

        if (OPRT_OK != tdl_audio_play(s_audio, (uint8_t *)&src[k * LB_FRAME_SAMPLES], LB_FRAME_BYTES)) {
            errors++;
        }
        due = start + (SYS_TIME_T)(k + 1) * LB_FRAME_MS;
        now = tal_system_get_millisecond();
        tal_system_sleep((due > now) ? (uint32_t)(due - now) : 1);
    }
    if (errors) {
        bk_printf("@LB-ERROR tdl_audio_play failed %u/%u frames\r\n", errors, frames);
    }
}

/* Record while the key is held, up to LB_MAX_SECONDS. */
static uint32_t lb_record(void)
{
    SYS_TIME_T start = tal_system_get_millisecond();
    uint32_t n;

    bk_printf("@LB-SAY RECORD: key down, speak; release to play (max %u s)\r\n", LB_MAX_SECONDS);
    lb_arm(false, s_voice_mic, s_voice_ref, LB_MAX_SAMPLES);
    while (s_key_down && s_rec_raw_n < LB_MAX_SAMPLES) {
        tal_system_sleep(LB_FRAME_MS);
    }
    n = lb_disarm();
    if (n >= LB_MAX_SAMPLES) {
        bk_printf("@LB-SAY max length reached, playing\r\n");
    }
    bk_printf("@LB-INFO recorded %u ms\r\n", (uint32_t)(tal_system_get_millisecond() - start));
    return n;
}

/* Play the recording while the mic captures the echo: raw or via AEC. */
static void lb_playback(uint32_t samples, uint32_t count, float *raw_echo_db)
{
    bool aec = (0 == (count % 2));
    float echo_db;
    char db[16];
    uint32_t n;

    bk_printf("@LB-SAY PLAYBACK %u: %u ms, echo path %s\r\n", count, samples * 1000 / LB_SAMPLE_HZ,
              aec ? "AEC" : "RAW");
    lb_arm(aec, s_echo, s_echo_ref, samples);
    lb_play(s_voice_mic, samples);
    /* Let the echo tail and the last frames arrive. */
    tal_system_sleep(200);
    n = lb_disarm();
    tkl_ai_set_vad_aec_algorithm(lb_bypass_hook);

    echo_db = lb_log_stats(aec ? "echo.aec_out" : "echo.mic", s_echo, n);
    if (!aec) {
        /* The loopback must follow the speaker and must not clip. */
        lb_log_stats("echo.ref", s_echo_ref, n);
        *raw_echo_db = echo_db;
    } else if (*raw_echo_db > -120.0f) {
        lb_format_db(db, sizeof(db), *raw_echo_db - echo_db);
        bk_printf("@LB-RESULT AEC echo %s dB below the previous raw playback\r\n", db);
    }
    bk_printf("@LB-SAY ready: hold the key to record\r\n");
}

static OPERATE_RET lb_key_open(void)
{
    OPERATE_RET rt = OPRT_OK;
    TDL_BUTTON_HANDLE key = NULL;
    TDL_BUTTON_CFG_T cfg = {
        .long_start_valid_time = 3000,
        .long_keep_timer = 1000,
        .button_debounce_time = 50,
        .button_repeat_valid_count = 2,
        .button_repeat_valid_time = 0,
    };

    TUYA_CALL_ERR_RETURN(tdl_button_create(BUTTON_NAME, &cfg, &key));
    tdl_button_event_register(key, TDL_BUTTON_PRESS_DOWN, lb_key_cb);
    tdl_button_event_register(key, TDL_BUTTON_PRESS_UP, lb_key_cb);
    return rt;
}

static void user_main(void)
{
    float raw_echo_db = -120.0f;
    uint32_t count = 0;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("Chat Robot T5-E1 push-to-talk mic + AEC test, log UART1 460800 8N1");

    if (OPRT_OK != board_register_hardware()) {
        PR_ERR("board_register_hardware failed; AW87390 I2C on GPIO38/39");
    }

    s_voice_mic = (int16_t *)tal_psram_malloc(LB_MAX_SAMPLES * sizeof(int16_t));
    s_voice_ref = (int16_t *)tal_psram_malloc(LB_MAX_SAMPLES * sizeof(int16_t));
    s_echo = (int16_t *)tal_psram_malloc(LB_MAX_SAMPLES * sizeof(int16_t));
    s_echo_ref = (int16_t *)tal_psram_malloc(LB_MAX_SAMPLES * sizeof(int16_t));
    if (NULL == s_voice_mic || NULL == s_voice_ref || NULL == s_echo || NULL == s_echo_ref) {
        bk_printf("@LB-ERROR psram alloc failed\r\n");
        goto halt;
    }

    /* Both must be set before tkl_ai_init() runs inside tdl_audio_open(). */
    tkl_ai_disable_vendor_vad();
    tkl_ai_set_vad_aec_algorithm(lb_bypass_hook);

    if (OPRT_OK != tdl_audio_find(AUDIO_CODEC_NAME, &s_audio) || OPRT_OK != tdl_audio_open(s_audio, lb_mic_cb) ||
        OPRT_OK != tdl_audio_volume_set(s_audio, LB_VOLUME)) {
        bk_printf("@LB-ERROR audio open failed\r\n");
        goto halt;
    }
    if (OPRT_OK != lb_key_open()) {
        bk_printf("@LB-ERROR key %s on GPIO3 not available\r\n", BUTTON_NAME);
        goto halt;
    }

    /* Skip start-up transients, then confirm both capture paths run. */
    tal_system_sleep(1000);
    bk_printf("@LB-INFO cb_frames=%u cb_len=%u hook_frames=%u expect_len=%u volume=%u\r\n", s_cb_frames,
              s_cb_last_len, s_hook_frames, LB_FRAME_BYTES, LB_VOLUME);
    if (0 == s_hook_frames) {
        bk_printf("@LB-ERROR AEC hook never ran: two-channel ADC path is not active\r\n");
    }
    bk_printf("@LB-SAY ready: hold the key to record\r\n");

    while (1) {
        uint32_t n;

        if (!s_key_down) {
            tal_system_sleep(LB_FRAME_MS);
            continue;
        }
        n = lb_record();
        lb_log_stats("voice.mic", s_voice_mic, n);
        lb_log_stats("voice.ref", s_voice_ref, n);
        if (n < LB_MIN_RECORD_MS * LB_SAMPLE_HZ / 1000) {
            bk_printf("@LB-SAY too short, hold the key while speaking\r\n");
            continue;
        }
        lb_playback(n, ++count, &raw_echo_db);
        /* A key held through playback starts no new recording until released. */
        while (s_key_down) {
            tal_system_sleep(LB_FRAME_MS);
        }
    }

halt:
    while (1) {
        tal_system_sleep(1000);
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
