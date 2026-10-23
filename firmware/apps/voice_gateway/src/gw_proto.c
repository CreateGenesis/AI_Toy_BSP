/**
 * @file gw_proto.c
 * @brief Big-endian packing for the voice Gateway TCP protocol.
 */

#include "gw_proto.h"

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put_u64(uint8_t *p, uint64_t v)
{
    put_u32(p, (uint32_t)(v >> 32));
    put_u32(p + 4, (uint32_t)v);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t get_u64(const uint8_t *p)
{
    return ((uint64_t)get_u32(p) << 32) | get_u32(p + 4);
}

void gw_put_tcp_header(uint8_t out[GW_TCP_HEADER_LEN], uint8_t type, uint32_t payload_len)
{
    put_u16(out, GW_TCP_MAGIC);
    out[2] = GW_VERSION;
    out[3] = type;
    put_u32(out + 4, payload_len);
}

int gw_parse_tcp_header(const uint8_t in[GW_TCP_HEADER_LEN], GW_TCP_HEADER_T *hdr)
{
    if (get_u16(in) != GW_TCP_MAGIC || in[2] != GW_VERSION) {
        return -1;
    }
    hdr->type = in[3];
    hdr->payload_len = get_u32(in + 4);
    return (hdr->payload_len > GW_MAX_TCP_PAYLOAD) ? -1 : 0;
}

void gw_put_audio_header(uint8_t out[GW_AUDIO_HEADER_LEN], uint8_t type, uint16_t flags, uint64_t session_id,
                         uint32_t sequence, uint64_t timestamp_ms, uint32_t payload_len)
{
    put_u16(out, GW_AUDIO_MAGIC);
    out[2] = GW_VERSION;
    out[3] = type;
    put_u16(out + 4, flags);
    put_u16(out + 6, GW_AUDIO_HEADER_LEN);
    put_u64(out + 8, session_id);
    put_u32(out + 16, sequence);
    put_u64(out + 20, timestamp_ms);
    put_u32(out + 28, payload_len);
}

int gw_parse_audio(const uint8_t *data, uint32_t len, GW_AUDIO_FRAME_T *frame)
{
    if (len < GW_AUDIO_HEADER_LEN || get_u16(data) != GW_AUDIO_MAGIC || data[2] != GW_VERSION ||
        get_u16(data + 6) != GW_AUDIO_HEADER_LEN) {
        return -1;
    }
    frame->type = data[3];
    frame->flags = get_u16(data + 4);
    frame->session_id = get_u64(data + 8);
    frame->sequence = get_u32(data + 16);
    frame->timestamp_ms = get_u64(data + 20);
    frame->payload_len = get_u32(data + 28);
    frame->payload = data + GW_AUDIO_HEADER_LEN;
    if (frame->payload_len != len - GW_AUDIO_HEADER_LEN || frame->payload_len > GW_MAX_AUDIO_PAYLOAD) {
        return -1;
    }
    return 0;
}

int gw_parse_session_id(const char *text, uint64_t *id)
{
    uint64_t v = 0;

    if (NULL == text || '\0' == *text) {
        return -1;
    }
    for (; *text; text++) {
        if (*text < '0' || *text > '9' || v > (UINT64_MAX - 9) / 10) {
            return -1;
        }
        v = v * 10 + (uint64_t)(*text - '0');
    }
    *id = v;
    return 0;
}
