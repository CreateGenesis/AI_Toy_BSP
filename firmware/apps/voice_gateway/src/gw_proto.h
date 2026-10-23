/**
 * @file gw_proto.h
 * @brief Wire format of the self-hosted voice Gateway (platform/gateway,
 *        src/protocol/t5.rs). Plain C, no SDK dependency.
 *
 * Every TCP message: 8-byte big-endian header !HBBI (magic 0x5450, version 1,
 * type, payload_len), then exactly payload_len bytes. Audio payloads start
 * with a 32-byte big-endian header !HBBHHQIQI. Fields are packed byte by
 * byte; never cast a C struct onto the wire.
 */

#ifndef GW_PROTO_H
#define GW_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define GW_TCP_MAGIC        0x5450
#define GW_AUDIO_MAGIC      0x5435
#define GW_VERSION          1
#define GW_TCP_HEADER_LEN   8
#define GW_AUDIO_HEADER_LEN 32

#define GW_MSG_JSON         0x01
#define GW_MSG_UPLINK_PCM   0x10
#define GW_MSG_DOWNLINK_MP3 0x11

#define GW_FLAG_START       0x0001
#define GW_FLAG_END         0x0002

/* Gateway limits: MAX_AUDIO_PAYLOAD and MAX_TCP_PAYLOAD in t5.rs. */
#define GW_MAX_AUDIO_PAYLOAD 3072
#define GW_MAX_TCP_PAYLOAD   (64 * 1024)

typedef struct {
    uint8_t type;
    uint32_t payload_len;
} GW_TCP_HEADER_T;

typedef struct {
    uint8_t type;
    uint16_t flags;
    uint64_t session_id;
    uint32_t sequence;
    uint64_t timestamp_ms;
    const uint8_t *payload;
    uint32_t payload_len;
} GW_AUDIO_FRAME_T;

void gw_put_tcp_header(uint8_t out[GW_TCP_HEADER_LEN], uint8_t type, uint32_t payload_len);

/* Returns 0, or -1 for a bad magic/version or an oversized payload. */
int gw_parse_tcp_header(const uint8_t in[GW_TCP_HEADER_LEN], GW_TCP_HEADER_T *hdr);

/* Writes the 32-byte audio header for payload_len bytes that follow it. */
void gw_put_audio_header(uint8_t out[GW_AUDIO_HEADER_LEN], uint8_t type, uint16_t flags, uint64_t session_id,
                         uint32_t sequence, uint64_t timestamp_ms, uint32_t payload_len);

/* Validates and decodes an audio payload (the TCP payload of type 0x10/0x11).
 * Returns 0, or -1 if the header or its payload_len is inconsistent. */
int gw_parse_audio(const uint8_t *data, uint32_t len, GW_AUDIO_FRAME_T *frame);

/* Parses the decimal session_id string. Returns 0, or -1 if not a u64. */
int gw_parse_session_id(const char *text, uint64_t *id);

#endif
