/**
 * @file voice_gateway.c
 * @brief Push-to-talk client of the self-hosted voice Gateway for the
 *        Chat Robot T5-E1 board.
 *
 * Wi-Fi station -> plain TCP to the Gateway (gw_config.h) -> auth -> hello ->
 * idle with ping. Holding the key opens a "hold" session and streams 16 kHz
 * mono PCM in 80 ms frames; releasing it ends the session. The Gateway
 * answers asr.final, reply.text, tts.start, MP3 chunks and tts.end; the MP3
 * is decoded with minimp3 and played through the verified speaker path.
 *
 * The protocol follows platform/gateway/src/protocol/t5.rs and main.rs:
 *   - The Gateway closes the TCP link on most framing errors without an
 *     error message, so a lost link is handled like any disconnect.
 *   - It runs ASR, LLM and TTS inside its read loop, so pongs are late while
 *     a reply is produced. Pings are sent only while idle.
 *   - A one-chunk MP3 carries START only; tts.end marks the end of audio.
 * Any failure closes the socket and reconnects after 3 s with a fresh
 * auth, hello and session.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_network.h"
#include "tal_wifi.h"
#include "tkl_output.h"

#include "board_com_api.h"
#include "tdl_audio_manage.h"
#include "tdl_button_manage.h"
#include "ws2812.h"
#include "cJSON.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "gw_config.h"
#include "gw_proto.h"
#include "auth_sdk_port.h"

#define GW_SAMPLE_HZ          16000
#define GW_FRAME_MS           80
#define GW_FRAME_BYTES        2560 /* 80 ms of 16-bit mono */
#define GW_PLAY_FRAME_SAMPLES 320  /* 20 ms per tdl_audio_play() */
#define GW_MAX_RECORD_S       30
#define GW_PCM_CAP            (GW_MAX_RECORD_S * GW_SAMPLE_HZ * 2)
#define GW_MP3_CAP            (512 * 1024)
#define GW_RECONNECT_MS       3000
#define GW_REPLY_TIMEOUT_MS   60000
#define GW_STEP_TIMEOUT_MS    5000
#define GW_PONG_TIMEOUT_MS    10000
#define GW_RECV_POLL_MS       1000
#define GW_JSON_CAP            4608

typedef enum {
    EV_AUTH_OK,
    EV_HELLO_OK,
    EV_PONG,
    EV_READY,
    EV_TTS_END,
    EV_ERROR,
    EV_DOWN,
} GW_EVENT_E;

typedef struct {
    uint8_t kind;
    char session_id[24];
    char request_id[32];
    char code[40];
} GW_EVENT_T;

typedef enum {
    GW_OK,
    GW_LINK_LOST, /* reconnect */
} GW_RESULT_E;

extern void bk_printf(const char *fmt, ...);

static TDL_AUDIO_HANDLE_T s_audio = NULL;
static QUEUE_HANDLE s_events = NULL;
static MUTEX_HANDLE s_tx_lock = NULL;
static THREAD_HANDLE s_rx_thread = NULL;
static volatile int s_fd = -1;
static volatile bool s_rx_run = false;
static volatile bool s_rx_alive = false;
static volatile bool s_wifi_up = false;
static volatile bool s_key_down = false;
static uint32_t s_ping_interval_ms = 20000;
static auth_sdk_port_t s_auth;
static bool s_auth_initialized = false;

static uint8_t *s_rx_buf = NULL;
static uint8_t s_tx_buf[GW_TCP_HEADER_LEN + GW_AUDIO_HEADER_LEN + GW_MAX_AUDIO_PAYLOAD];

/* Microphone capture of the current utterance, filled by the mic callback. */
static uint8_t *s_pcm = NULL;
static volatile uint32_t s_pcm_len = 0;
static volatile bool s_recording = false;

/* MP3 of the current reply, filled by the receive thread. */
static uint8_t *s_mp3 = NULL;
static volatile uint32_t s_mp3_len = 0;
static volatile uint64_t s_tts_session = 0;
static uint32_t s_mp3_next_seq = 1;
static uint32_t s_mp3_chunks = 0;
static bool s_mp3_overflow = false;

/* ---------------------------------------------------------------- logging */

/* Prints UTF-8 text in short pieces. Never passes it as a format string:
 * replies may contain '%', and the log pool truncates long lines. */
static void gw_log_text(const char *label, const char *text)
{
    char piece[96];
    size_t len = strlen(text);
    size_t off = 0;

    bk_printf("[gw] %s (%u bytes):\r\n", label, (unsigned)len);
    while (off < len) {
        size_t n = len - off;
        if (n > sizeof(piece) - 1) {
            n = sizeof(piece) - 1;
            /* Back up to a UTF-8 lead byte so a character is not split. */
            while (n > 0 && ((uint8_t)text[off + n] & 0xC0) == 0x80) {
                n--;
            }
        }
        memcpy(piece, &text[off], n);
        piece[n] = '\0';
        bk_printf("%s\r\n", piece);
        off += n;
    }
}

/* ------------------------------------------------------------------ audio */

static void gw_mic_cb(TDL_AUDIO_FRAME_FORMAT_E type, TDL_AUDIO_STATUS_E status, uint8_t *data, uint32_t len)
{
    (void)status;

    if (!s_recording || TDL_AUDIO_FRAME_FORMAT_PCM != type) {
        return;
    }
    if (s_pcm_len + len > GW_PCM_CAP) {
        len = GW_PCM_CAP - s_pcm_len;
    }
    memcpy(&s_pcm[s_pcm_len], data, len);
    s_pcm_len += len;
}

static void gw_key_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E event, void *arg)
{
    (void)name;
    (void)arg;

    s_key_down = (TDL_BUTTON_PRESS_DOWN == event);
}

/* Decodes the reply MP3 and plays it 20 ms at a time. Sleeping after every
 * frame lets the speaker task (priority 5) drain the DAC. minimp3 keeps a
 * ~16 KB scratch area on the stack. */
static void gw_play_mp3(const uint8_t *mp3, uint32_t len)
{
    static mp3dec_t dec;
    static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    static int16_t frame[GW_PLAY_FRAME_SAMPLES];
    mp3dec_frame_info_t info;
    uint32_t off = 0, fill = 0, played = 0;
    SYS_TIME_T start = tal_system_get_millisecond();
    bool warned = false;

    mp3dec_init(&dec);
    while (off < len) {
        int samples = mp3dec_decode_frame(&dec, &mp3[off], (int)(len - off), pcm, &info);
        int i;

        if (0 == info.frame_bytes) {
            break; /* no further frame in the buffer */
        }
        off += (uint32_t)info.frame_bytes;
        if (samples <= 0) {
            continue; /* ID3 tag or skipped data */
        }
        if (!warned && (GW_SAMPLE_HZ != info.hz || 1 != info.channels)) {
            bk_printf("[gw] mp3 is %d Hz %d ch, played as 16 kHz mono\r\n", info.hz, info.channels);
            warned = true;
        }
        for (i = 0; i < samples; i++) {
            /* Interleaved stereo: keep the left channel. */
            frame[fill++] = pcm[i * info.channels];
            if (GW_PLAY_FRAME_SAMPLES == fill) {
                SYS_TIME_T due, now;

                tdl_audio_play(s_audio, (uint8_t *)frame, sizeof(frame));
                fill = 0;
                played++;
                due = start + (SYS_TIME_T)played * 20;
                now = tal_system_get_millisecond();
                tal_system_sleep((due > now) ? (uint32_t)(due - now) : 1);
            }
        }
    }
    if (fill) {
        memset(&frame[fill], 0, (GW_PLAY_FRAME_SAMPLES - fill) * sizeof(int16_t));
        tdl_audio_play(s_audio, (uint8_t *)frame, sizeof(frame));
        played++;
    }
    tal_system_sleep(300); /* let the DAC finish the queued frames */
    bk_printf("[gw] played %u ms of reply audio\r\n", played * 20);
}

/* ---------------------------------------------------------------- sending */

static int gw_send_raw(const uint8_t *buf, uint32_t len)
{
    uint32_t sent = 0;

    while (sent < len) {
        int r = tal_net_send(s_fd, buf + sent, len - sent);
        if (r <= 0) {
            return -1;
        }
        sent += (uint32_t)r;
    }
    return 0;
}

static int gw_send_json(const char *json)
{
    uint32_t len = (uint32_t)strlen(json);
    uint8_t hdr[GW_TCP_HEADER_LEN];
    int rt;

    gw_put_tcp_header(hdr, GW_MSG_JSON, len);
    tal_mutex_lock(s_tx_lock);
    rt = gw_send_raw(hdr, sizeof(hdr));
    if (0 == rt) {
        rt = gw_send_raw((const uint8_t *)json, len);
    }
    tal_mutex_unlock(s_tx_lock);
    if (0 != rt) {
        bk_printf("[gw] send json failed\r\n");
    }
    return rt;
}

/* Formats and sends a JSON message. Refuses to send a truncated message:
 * the Gateway drops the link on invalid JSON without an error reply. */
static int gw_send_jsonf(const char *fmt, ...)
{
    char json[GW_JSON_CAP];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(json, sizeof(json), fmt, args);
    va_end(args);
    if (n < 0 || n >= (int)sizeof(json)) {
        bk_printf("[gw] json message too long (%d bytes), not sent\r\n", n);
        return -1;
    }
    return gw_send_json(json);
}

static bool gw_auth_configured(void)
{
    return GW_AUTH_BASE_URL[0] != '\0' && GW_AUTH_ENDPOINT_ID[0] != '\0';
}

/* Auth is prepared only after Wi-Fi has an IP. The local token remains an
 * explicit development fallback until the production endpoint is configured. */
static int gw_auth_prepare(void)
{
    const char *token = NULL;
    int result;

    if (!gw_auth_configured()) {
        if (GW_AUTH_REQUIRED) {
            bk_printf("[gw] Auth SDK is required but GW_AUTH_BASE_URL/ENDPOINT_ID is empty\r\n");
            return -1;
        }
        s_auth_initialized = false;
        return 0;
    }
    if (!s_auth_initialized) {
        result = auth_sdk_port_init(&s_auth, GW_AUTH_BASE_URL,
                                    GW_AUTH_ACTIVATION_CODE[0] ? GW_AUTH_ACTIVATION_CODE : NULL,
                                    GW_AUTH_ENDPOINT_ID);
        if (result != AUTH_DEVICE_OK) {
            bk_printf("[gw] Auth SDK init failed: %d\r\n", result);
            return GW_AUTH_REQUIRED ? -1 : 0;
        }
        s_auth_initialized = true;
        bk_printf("[gw] Auth SDK initialized\r\n");
    }
    result = auth_sdk_port_get_token(&s_auth, &token);
    if (result != AUTH_DEVICE_OK || !token || !*token) {
        bk_printf("[gw] Auth token request failed: %d\r\n", result);
        return GW_AUTH_REQUIRED ? -1 : 0;
    }
    return 0;
}

static const char *gw_auth_token(void)
{
    const char *token = NULL;

    if (s_auth_initialized && auth_sdk_port_get_token(&s_auth, &token) == AUTH_DEVICE_OK &&
        token && *token) {
        return token;
    }
    return GW_TOKEN;
}

static const char *gw_auth_device_id(void)
{
    if (s_auth_initialized && s_auth.client.device_id[0]) {
        return s_auth.client.device_id;
    }
    return GW_DEVICE_ID;
}

static int gw_send_pcm(uint64_t session_id, uint32_t seq, uint16_t flags, const uint8_t *pcm, uint32_t len)
{
    uint32_t total = GW_TCP_HEADER_LEN + GW_AUDIO_HEADER_LEN + len;
    int rt;

    tal_mutex_lock(s_tx_lock);
    gw_put_tcp_header(s_tx_buf, GW_MSG_UPLINK_PCM, GW_AUDIO_HEADER_LEN + len);
    gw_put_audio_header(&s_tx_buf[GW_TCP_HEADER_LEN], GW_MSG_UPLINK_PCM, flags, session_id, seq,
                        (uint64_t)tal_system_get_millisecond(), len);
    memcpy(&s_tx_buf[GW_TCP_HEADER_LEN + GW_AUDIO_HEADER_LEN], pcm, len);
    rt = gw_send_raw(s_tx_buf, total);
    tal_mutex_unlock(s_tx_lock);
    if (0 != rt) {
        bk_printf("[gw] send pcm seq %u failed\r\n", seq);
    }
    return rt;
}

/* -------------------------------------------------------------- receiving */

static void gw_post(uint8_t kind, const char *session_id, const char *request_id, const char *code)
{
    GW_EVENT_T ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    if (session_id) {
        strncpy(ev.session_id, session_id, sizeof(ev.session_id) - 1);
    }
    if (request_id) {
        strncpy(ev.request_id, request_id, sizeof(ev.request_id) - 1);
    }
    if (code) {
        strncpy(ev.code, code, sizeof(ev.code) - 1);
    }
    tal_queue_post(s_events, &ev, 0);
}

/* Reads exactly len bytes, across short reads and receive timeouts. */
static int gw_recv_exact(uint8_t *buf, uint32_t len)
{
    uint32_t got = 0;

    while (got < len) {
        int r = tal_net_recv(s_fd, buf + got, len - got);
        if (r > 0) {
            got += (uint32_t)r;
            continue;
        }
        if (r < 0) {
            TUYA_ERRNO err = tal_net_get_errno();
            if ((UNW_EAGAIN == err || UNW_EWOULDBLOCK == err) && s_rx_run) {
                continue;
            }
        }
        return -1; /* closed by the peer, error, or stop requested */
    }
    return 0;
}

static const char *gw_json_str(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static void gw_handle_json(const uint8_t *payload, uint32_t len)
{
    cJSON *msg = cJSON_ParseWithLength((const char *)payload, len);
    const char *type;

    if (NULL == msg) {
        bk_printf("[gw] invalid JSON from gateway (%u bytes)\r\n", len);
        return;
    }
    type = gw_json_str(msg, "type");
    if (NULL == type) {
        type = "";
    }

    if (0 == strcmp(type, "auth.ok")) {
        gw_post(EV_AUTH_OK, NULL, NULL, NULL);
    } else if (0 == strcmp(type, "hello.ok")) {
        const cJSON *ping = cJSON_GetObjectItemCaseSensitive(msg, "ping_interval_s");
        if (cJSON_IsNumber(ping) && ping->valueint > 0) {
            s_ping_interval_ms = (uint32_t)ping->valueint * 1000;
        }
        gw_post(EV_HELLO_OK, NULL, NULL, NULL);
    } else if (0 == strcmp(type, "pong")) {
        gw_post(EV_PONG, NULL, NULL, NULL);
    } else if (0 == strcmp(type, "session.ready")) {
        gw_post(EV_READY, gw_json_str(msg, "session_id"), gw_json_str(msg, "request_id"), NULL);
    } else if (0 == strcmp(type, "asr.final")) {
        const char *text = gw_json_str(msg, "text");
        gw_log_text("asr.final", text ? text : "");
    } else if (0 == strcmp(type, "reply.text")) {
        const char *text = gw_json_str(msg, "text");
        gw_log_text("reply.text", text ? text : "");
    } else if (0 == strcmp(type, "tts.start")) {
        const char *sid = gw_json_str(msg, "session_id");
        const char *codec = gw_json_str(msg, "codec");
        uint64_t id = 0;

        /* Reset here, before the first MP3 chunk can arrive on this thread. */
        gw_parse_session_id(sid, &id);
        s_tts_session = id;
        s_mp3_len = 0;
        s_mp3_next_seq = 1;
        s_mp3_chunks = 0;
        s_mp3_overflow = false;
        bk_printf("[gw] tts.start codec=%s\r\n", codec ? codec : "?");
    } else if (0 == strcmp(type, "tts.end")) {
        bk_printf("[gw] tts.end: %u chunks, %u bytes\r\n", s_mp3_chunks, s_mp3_len);
        gw_post(EV_TTS_END, gw_json_str(msg, "session_id"), NULL, NULL);
    } else if (0 == strcmp(type, "error")) {
        const char *code = gw_json_str(msg, "code");
        const char *text = gw_json_str(msg, "message");
        bk_printf("[gw] error %s\r\n", code ? code : "?");
        if (text) {
            gw_log_text("error.message", text);
        }
        gw_post(EV_ERROR, gw_json_str(msg, "session_id"), NULL, code ? code : "");
    } else {
        bk_printf("[gw] ignored message type \"%s\"\r\n", type);
    }
    cJSON_Delete(msg);
}

static void gw_handle_mp3(const uint8_t *payload, uint32_t len)
{
    GW_AUDIO_FRAME_T f;

    if (0 != gw_parse_audio(payload, len, &f) || GW_MSG_DOWNLINK_MP3 != f.type) {
        bk_printf("[gw] bad mp3 frame (%u bytes)\r\n", len);
        return;
    }
    if (f.session_id != s_tts_session) {
        bk_printf("[gw] mp3 for another session, dropped\r\n");
        return;
    }
    if (f.sequence != s_mp3_next_seq) {
        bk_printf("[gw] mp3 sequence %u, expected %u\r\n", f.sequence, s_mp3_next_seq);
    }
    s_mp3_next_seq = f.sequence + 1;
    s_mp3_chunks++;
    if (s_mp3_len + f.payload_len > GW_MP3_CAP) {
        s_mp3_overflow = true;
        return;
    }
    memcpy(&s_mp3[s_mp3_len], f.payload, f.payload_len);
    s_mp3_len += f.payload_len;
}

static void gw_rx_thread(void *arg)
{
    (void)arg;

    while (s_rx_run) {
        GW_TCP_HEADER_T hdr;

        if (0 != gw_recv_exact(s_rx_buf, GW_TCP_HEADER_LEN)) {
            break;
        }
        if (0 != gw_parse_tcp_header(s_rx_buf, &hdr)) {
            bk_printf("[gw] bad TCP header from gateway\r\n");
            break;
        }
        if (0 != gw_recv_exact(s_rx_buf, hdr.payload_len)) {
            break;
        }
        if (GW_MSG_JSON == hdr.type) {
            gw_handle_json(s_rx_buf, hdr.payload_len);
        } else if (GW_MSG_DOWNLINK_MP3 == hdr.type) {
            gw_handle_mp3(s_rx_buf, hdr.payload_len);
        } else {
            bk_printf("[gw] ignored TCP type 0x%02x\r\n", hdr.type);
        }
    }
    if (s_rx_run) {
        bk_printf("[gw] connection closed by gateway\r\n");
        gw_post(EV_DOWN, NULL, NULL, NULL);
    }
    s_rx_alive = false;
    tal_thread_delete(s_rx_thread);
}

/* ----------------------------------------------------------- connection */

static void gw_disconnect(void)
{
    s_rx_run = false;
    while (s_rx_alive) {
        tal_system_sleep(50); /* recv wakes up within GW_RECV_POLL_MS */
    }
    if (s_fd >= 0) {
        tal_net_close(s_fd);
        s_fd = -1;
    }
    tal_queue_free(s_events);
    tal_queue_create_init(&s_events, sizeof(GW_EVENT_T), 16);
}

static int gw_connect(void)
{
    TUYA_IP_ADDR_T addr = tal_net_str2addr(GW_SERVER_IP);
    THREAD_CFG_T cfg = {
        .stackDepth = 8192,
        .priority = THREAD_PRIO_2,
        .thrdname = "gw_rx",
    };
    int fd;

    fd = tal_net_socket_create(PROTOCOL_TCP);
    if (fd < 0) {
        bk_printf("[gw] socket create failed\r\n");
        return -1;
    }
    bk_printf("[gw] connecting %s:%d\r\n", GW_SERVER_IP, GW_SERVER_PORT);
    if (0 != tal_net_connect(fd, addr, GW_SERVER_PORT)) {
        bk_printf("[gw] connect failed, errno %d (gateway running? firewall open for TCP %d?)\r\n",
                  tal_net_get_errno(), GW_SERVER_PORT);
        tal_net_close(fd);
        return -1;
    }
    tal_net_disable_nagle(fd);
    tal_net_set_keepalive(fd, TRUE, 30, 5, 3);
    tal_net_set_timeout(fd, GW_RECV_POLL_MS, TRANS_RECV);
    s_fd = fd;

    s_rx_run = true;
    s_rx_alive = true;
    if (OPRT_OK != tal_thread_create_and_start(&s_rx_thread, NULL, NULL, gw_rx_thread, NULL, &cfg)) {
        s_rx_alive = false;
        return -1;
    }
    bk_printf("[gw] TCP connected\r\n");
    return 0;
}

/* Waits for one of the wanted events; any EV_ERROR or EV_DOWN also returns. */
static bool gw_wait(uint32_t wanted_mask, uint32_t timeout_ms, GW_EVENT_T *ev)
{
    SYS_TIME_T end = tal_system_get_millisecond() + timeout_ms;

    memset(ev, 0, sizeof(*ev));
    ev->kind = 0xFF; /* timeout */
    while (1) {
        SYS_TIME_T now = tal_system_get_millisecond();
        if (now >= end) {
            return false;
        }
        if (OPRT_OK != tal_queue_fetch(s_events, ev, (uint32_t)(end - now))) {
            return false;
        }
        if ((wanted_mask & (1u << ev->kind)) || EV_ERROR == ev->kind || EV_DOWN == ev->kind) {
            return true;
        }
        /* EV_PONG while waiting for something else is harmless. */
    }
}

static GW_RESULT_E gw_handshake(void)
{
    GW_EVENT_T ev;
    const char *token = gw_auth_token();
    const char *device_id = gw_auth_device_id();

    if (gw_send_jsonf("{\"type\":\"auth\",\"token\":\"%s\",\"device_id\":\"%s\",\"firmware\":\"%s\"}",
                      token, device_id, GW_FIRMWARE) != 0) {
        return GW_LINK_LOST;
    }
    if (!gw_wait(1u << EV_AUTH_OK, GW_STEP_TIMEOUT_MS, &ev) || EV_AUTH_OK != ev.kind) {
        bk_printf("[gw] auth failed (%s); check DEVICE_ID/TOKEN/FIRMWARE_VERSION in platform/.env\r\n",
                  EV_ERROR == ev.kind ? ev.code : "no auth.ok");
        return GW_LINK_LOST;
    }
    bk_printf("[gw] auth.ok\r\n");

    if (gw_send_jsonf("{\"type\":\"hello\",\"protocol_version\":1,\"device_id\":\"%s\",\"firmware\":\"%s\","
                      "\"audio\":{\"up\":[\"pcm_s16le\"],\"down\":[\"mp3\"],\"sample_rate\":16000,\"channels\":1,"
                      "\"bits_per_sample\":16,\"frame_ms\":80}}", device_id, GW_FIRMWARE) != 0) {
        return GW_LINK_LOST;
    }
    /* INVALID_HELLO leaves the link open but unusable: reconnect. */
    if (!gw_wait(1u << EV_HELLO_OK, GW_STEP_TIMEOUT_MS, &ev) || EV_HELLO_OK != ev.kind) {
        bk_printf("[gw] hello failed (%s)\r\n", EV_ERROR == ev.kind ? ev.code : "no hello.ok");
        return GW_LINK_LOST;
    }
    bk_printf("[gw] hello.ok, idle. Hold the key to talk\r\n");
    return GW_OK;
}

/* ------------------------------------------------------------- session */

/* Sends complete frames recorded so far, keeping the newest one back so the
 * final frame can still carry END. */
static int gw_stream_ready(uint64_t sid, uint32_t *seq, uint32_t *sent)
{
    while (s_pcm_len >= *sent + 2 * GW_FRAME_BYTES) {
        uint16_t flags = (1 == *seq) ? GW_FLAG_START : 0;
        if (0 != gw_send_pcm(sid, *seq, flags, &s_pcm[*sent], GW_FRAME_BYTES)) {
            return -1;
        }
        *sent += GW_FRAME_BYTES;
        (*seq)++;
    }
    return 0;
}

/* After the key is released: everything left, the last frame with END. A
 * single frame gets START|END. No audio at all sends no frame. */
static int gw_stream_finish(uint64_t sid, uint32_t *seq, uint32_t *sent)
{
    uint32_t total = s_pcm_len;

    while (*sent < total) {
        uint32_t n = total - *sent;
        uint16_t flags = (1 == *seq) ? GW_FLAG_START : 0;

        if (n > GW_FRAME_BYTES) {
            n = GW_FRAME_BYTES;
        }
        if (*sent + n == total) {
            flags |= GW_FLAG_END;
        }
        if (0 != gw_send_pcm(sid, *seq, flags, &s_pcm[*sent], n)) {
            return -1;
        }
        *sent += n;
        (*seq)++;
    }
    return 0;
}

static GW_RESULT_E gw_session(void)
{
    static uint32_t counter = 0;
    char request_id[32];
    char sid_text[24];
    uint64_t sid = 0;
    uint32_t seq = 1, sent = 0;
    SYS_TIME_T start = tal_system_get_millisecond();
    GW_EVENT_T ev;
    bool ready = false;

    /* Record from the key press so the first syllable survives the
     * session.ready round trip; nothing is sent before ready. */
    s_pcm_len = 0;
    s_recording = true;
    snprintf(request_id, sizeof(request_id), "t5-%u-%u", (unsigned)start, (unsigned)++counter);
    bk_printf("[gw] key down: session.start %s\r\n", request_id);
    if (0 != gw_send_jsonf("{\"type\":\"session.start\",\"request_id\":\"%s\",\"mode\":\"hold\",\"audio\":{"
                           "\"codec\":\"pcm_s16le\",\"sample_rate\":16000,\"channels\":1,\"bits_per_sample\":16,"
                           "\"frame_ms\":80}}",
                           request_id)) {
        s_recording = false;
        return GW_LINK_LOST;
    }

    while (1) {
        bool key = s_key_down && (tal_system_get_millisecond() - start) < GW_MAX_RECORD_S * 1000;

        if (!ready) {
            if (gw_wait(1u << EV_READY, 20, &ev)) {
                if (EV_DOWN == ev.kind) {
                    s_recording = false;
                    return GW_LINK_LOST;
                }
                if (EV_ERROR == ev.kind) {
                    s_recording = false;
                    return GW_OK; /* e.g. SESSION_ALREADY_ACTIVE: back to idle */
                }
                if (0 != strcmp(ev.request_id, request_id) || 0 != gw_parse_session_id(ev.session_id, &sid)) {
                    bk_printf("[gw] unexpected session.ready, reconnecting\r\n");
                    s_recording = false;
                    return GW_LINK_LOST;
                }
                strncpy(sid_text, ev.session_id, sizeof(sid_text));
                ready = true;
                bk_printf("[gw] session.ready %s after %u ms\r\n", sid_text,
                          (unsigned)(tal_system_get_millisecond() - start));
            } else if (tal_system_get_millisecond() - start > GW_STEP_TIMEOUT_MS) {
                bk_printf("[gw] no session.ready, reconnecting\r\n");
                s_recording = false;
                return GW_LINK_LOST;
            }
        }
        if (!key) {
            break;
        }
        if (ready && 0 != gw_stream_ready(sid, &seq, &sent)) {
            s_recording = false;
            return GW_LINK_LOST;
        }
        tal_system_sleep(20);
    }

    /* Key released: wait for the last mic frames, stop the microphone. */
    tal_system_sleep(60);
    s_recording = false;
    bk_printf("[gw] key up: %u ms of audio\r\n", s_pcm_len * 1000 / (GW_SAMPLE_HZ * 2));
    while (!ready) {
        if (!gw_wait(1u << EV_READY, GW_STEP_TIMEOUT_MS, &ev) || EV_DOWN == ev.kind) {
            return GW_LINK_LOST;
        }
        if (EV_ERROR == ev.kind) {
            return GW_OK;
        }
        if (0 != strcmp(ev.request_id, request_id) || 0 != gw_parse_session_id(ev.session_id, &sid)) {
            return GW_LINK_LOST;
        }
        strncpy(sid_text, ev.session_id, sizeof(sid_text));
        ready = true;
    }
    if (0 != gw_stream_ready(sid, &seq, &sent) || 0 != gw_stream_finish(sid, &seq, &sent)) {
        return GW_LINK_LOST;
    }
    if (0 != gw_send_jsonf("{\"type\":\"session.end\",\"session_id\":\"%s\"}", sid_text)) {
        return GW_LINK_LOST;
    }
    bk_printf("[gw] session.end after %u PCM frames, waiting for the reply\r\n", seq - 1);

    /* asr.final and reply.text are logged by the receive thread. */
    if (!gw_wait(1u << EV_TTS_END, GW_REPLY_TIMEOUT_MS, &ev)) {
        bk_printf("[gw] no reply within %u s, reconnecting\r\n", GW_REPLY_TIMEOUT_MS / 1000);
        return GW_LINK_LOST;
    }
    if (EV_DOWN == ev.kind) {
        return GW_LINK_LOST;
    }
    if (EV_ERROR == ev.kind) {
        return GW_OK; /* e.g. VOICE_PIPELINE_FAILED or PCM_END_REQUIRED */
    }
    if (s_mp3_overflow) {
        bk_printf("[gw] reply mp3 larger than %u bytes, truncated\r\n", GW_MP3_CAP);
    }
    /* Microphone stays off and the key is ignored while the reply plays. */
    gw_play_mp3(s_mp3, s_mp3_len);
    return GW_OK;
}

/* Idle until the key is pressed. Pings every ping_interval_s; a missing
 * pong while idle means a dead link. */
static GW_RESULT_E gw_idle(void)
{
    SYS_TIME_T next_ping = tal_system_get_millisecond() + s_ping_interval_ms;
    SYS_TIME_T pong_deadline = 0;
    GW_EVENT_T ev;

    while (1) {
        SYS_TIME_T now = tal_system_get_millisecond();

        while (OPRT_OK == tal_queue_fetch(s_events, &ev, 0)) {
            if (EV_DOWN == ev.kind) {
                return GW_LINK_LOST;
            }
            if (EV_PONG == ev.kind) {
                pong_deadline = 0;
            }
        }
        if (pong_deadline && now > pong_deadline) {
            bk_printf("[gw] no pong, reconnecting\r\n");
            return GW_LINK_LOST;
        }
        if (now >= next_ping) {
            if (0 != gw_send_jsonf("{\"type\":\"ping\",\"timestamp_ms\":%lu}", (unsigned long)now)) {
                return GW_LINK_LOST;
            }
            next_ping = now + s_ping_interval_ms;
            if (0 == pong_deadline) {
                pong_deadline = now + GW_PONG_TIMEOUT_MS;
            }
        }
        if (s_key_down) {
            GW_RESULT_E rt = gw_session();
            if (GW_OK != rt) {
                return rt;
            }
            while (s_key_down) {
                tal_system_sleep(20); /* release before the next round */
            }
            bk_printf("[gw] idle. Hold the key to talk\r\n");
            next_ping = tal_system_get_millisecond() + s_ping_interval_ms;
            pong_deadline = 0;
            continue;
        }
        tal_system_sleep(20);
    }
}

/* ----------------------------------------------------------------- setup */

static void gw_wifi_cb(WF_EVENT_E event, void *arg)
{
    (void)arg;
    if (WFE_CONNECTED == event) {
        bk_printf("[gw] wifi event: associated\r\n");
    } else if (WFE_CONNECT_FAILED == event) {
        bk_printf("[gw] wifi event: connect failed\r\n");
        s_wifi_up = false;
    } else if (WFE_DISCONNECTED == event) {
        bk_printf("[gw] wifi event: disconnected\r\n");
        s_wifi_up = false;
    }
}

static const char *gw_wifi_stat_text(WF_STATION_STAT_E stat)
{
    switch (stat) {
    case WSS_IDLE:
        return "idle";
    case WSS_CONNECTING:
        return "connecting";
    case WSS_PASSWD_WRONG:
        return "PASSWORD WRONG";
    case WSS_NO_AP_FOUND:
        return "AP NOT FOUND";
    case WSS_CONN_FAIL:
        return "CONNECT FAILED";
    case WSS_CONN_SUCCESS:
        return "associated, waiting for DHCP";
    case WSS_GOT_IP:
        return "got ip";
    case WSS_DHCP_FAIL:
        return "DHCP FAILED";
    default:
        return "unknown";
    }
}

/* Lists the visible access points so a missing, 5 GHz-only or weak SSID
 * shows up in the log. The T5 radio is 2.4 GHz only. */
static void gw_wifi_scan(void)
{
    AP_IF_S *aps = NULL;
    uint32_t num = 0, i;
    bool found = false;

    if (OPRT_OK != tal_wifi_all_ap_scan(&aps, &num) || NULL == aps) {
        bk_printf("[gw] wifi scan failed\r\n");
        return;
    }
    bk_printf("[gw] wifi scan: %u access points\r\n", num);
    for (i = 0; i < num; i++) {
        bool match = (0 == strcmp((const char *)aps[i].ssid, GW_WIFI_SSID));

        found = found || match;
        if (match || i < 20) {
            bk_printf("[gw]   %s ch %u rssi %d sec %u%s\r\n", aps[i].ssid, aps[i].channel, aps[i].rssi,
                      aps[i].security, match ? "  <== target" : "");
        }
    }
    if (!found) {
        bk_printf("[gw] \"%s\" not visible: check the name (case, spaces), 2.4 GHz band, distance\r\n",
                  GW_WIFI_SSID);
    }
    tal_wifi_release_ap(aps);
}

/* Connects and waits for an IP address, logging every station state. */
static void gw_wifi_wait(void)
{
    WF_STATION_STAT_E stat = WSS_IDLE, last = (WF_STATION_STAT_E)0xFF;
    uint32_t waited = 0;
    NW_IP_S ip;

    while (1) {
        if (0 == waited % 20000) {
            if (waited) {
                bk_printf("[gw] no IP after 20 s, retrying\r\n");
                tal_wifi_station_disconnect();
                tal_system_sleep(500);
            }
            gw_wifi_scan();
            bk_printf("[gw] joining wifi \"%s\" (password %u chars)\r\n", GW_WIFI_SSID,
                      (unsigned)strlen(GW_WIFI_PASSWORD));
            tal_wifi_station_connect((int8_t *)GW_WIFI_SSID, (int8_t *)GW_WIFI_PASSWORD);
            last = (WF_STATION_STAT_E)0xFF;
        }
        tal_system_sleep(500);
        waited += 500;

        if (OPRT_OK != tal_wifi_station_get_status(&stat)) {
            continue;
        }
        if (stat != last) {
            bk_printf("[gw] wifi status: %s\r\n", gw_wifi_stat_text(stat));
            last = stat;
        }
        if (WSS_GOT_IP == stat) {
            memset(&ip, 0, sizeof(ip));
            tal_wifi_get_ip(WF_STATION, &ip);
            bk_printf("[gw] wifi connected, ip %s mask %s gw %s\r\n", ip.ip, ip.mask, ip.gw);
            s_wifi_up = true;
            return;
        }
        if (WSS_PASSWD_WRONG == stat || WSS_NO_AP_FOUND == stat || WSS_CONN_FAIL == stat || WSS_DHCP_FAIL == stat) {
            /* Retry with a fresh scan after a short pause. */
            tal_system_sleep(3000);
            tal_wifi_station_disconnect();
            tal_system_sleep(500);
            waited = 0;
        }
    }
}

static OPERATE_RET gw_setup(void)
{
    OPERATE_RET rt = OPRT_OK;
    TDL_BUTTON_HANDLE key = NULL;
    TDL_BUTTON_CFG_T key_cfg = {
        .long_start_valid_time = 3000,
        .long_keep_timer = 1000,
        .button_debounce_time = 50,
        .button_repeat_valid_count = 2,
        .button_repeat_valid_time = 0,
    };

    s_pcm = (uint8_t *)tal_psram_malloc(GW_PCM_CAP);
    s_mp3 = (uint8_t *)tal_psram_malloc(GW_MP3_CAP);
    s_rx_buf = (uint8_t *)tal_psram_malloc(GW_MAX_TCP_PAYLOAD + 1);
    TUYA_CHECK_NULL_RETURN(s_pcm, OPRT_MALLOC_FAILED);
    TUYA_CHECK_NULL_RETURN(s_mp3, OPRT_MALLOC_FAILED);
    TUYA_CHECK_NULL_RETURN(s_rx_buf, OPRT_MALLOC_FAILED);
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_tx_lock));
    TUYA_CALL_ERR_RETURN(tal_queue_create_init(&s_events, sizeof(GW_EVENT_T), 16));

    bk_printf("[gw] opening audio\r\n");
    TUYA_CALL_ERR_RETURN(tdl_audio_find(AUDIO_CODEC_NAME, &s_audio));
    TUYA_CALL_ERR_RETURN(tdl_audio_open(s_audio, gw_mic_cb));
    TUYA_CALL_ERR_RETURN(tdl_audio_volume_set(s_audio, GW_VOLUME));
    bk_printf("[gw] audio ready\r\n");

    TUYA_CALL_ERR_RETURN(tdl_button_create(BUTTON_NAME, &key_cfg, &key));
    tdl_button_event_register(key, TDL_BUTTON_PRESS_DOWN, gw_key_cb);
    tdl_button_event_register(key, TDL_BUTTON_PRESS_UP, gw_key_cb);

    TUYA_CALL_ERR_RETURN(tal_wifi_init(gw_wifi_cb));
    TUYA_CALL_ERR_RETURN(tal_wifi_set_work_mode(WWM_STATION));
    return rt;
}

static void user_main(void)
{
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);
    PR_NOTICE("Chat Robot T5-E1 voice gateway client, log UART1 460800 8N1");
    PR_NOTICE("gateway %s:%d device %s firmware %s", GW_SERVER_IP, GW_SERVER_PORT, GW_DEVICE_ID, GW_FIRMWARE);

    if (OPRT_OK != board_register_hardware()) {
        PR_ERR("board_register_hardware failed; AW87390 I2C on GPIO38/39");
    }
    if (OPRT_OK != gw_setup()) {
        PR_ERR("setup failed");
        while (1) {
            tal_system_sleep(1000);
        }
    }
    {
        /* Started after the audio driver is up. Pixel 0 (nearest GPIO9) red,
         * pixel 1 green, steady, 30% brightness. */
        static const CHAT_ROBOT_RGB_T leds[CHAT_ROBOT_WS2812_COUNT] = {{77, 0, 0}, {0, 77, 0}};
        bk_printf("[gw] starting LEDs\r\n");
        if (OPRT_OK != chat_robot_ws2812_hold(leds)) {
            PR_ERR("WS2812 LEDs on GPIO9 not started");
        }
    }

    while (1) {
        if (!s_wifi_up) {
            gw_wifi_wait();
        }
        if (0 == gw_auth_prepare() && 0 == gw_connect() && GW_OK == gw_handshake()) {
            gw_idle();
        }
        gw_disconnect();
        bk_printf("[gw] reconnecting in %u s\r\n", GW_RECONNECT_MS / 1000);
        tal_system_sleep(GW_RECONNECT_MS);
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
    /* minimp3 decodes on this thread with ~16 KB of stack scratch. */
    THREAD_CFG_T thrd_param = {
        .stackDepth = 32 * 1024,
        .priority = THREAD_PRIO_2,
        .thrdname = "tuya_app_main",
    };
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
