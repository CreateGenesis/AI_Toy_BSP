#ifndef TOY_TRANSPORT_PORT_H
#define TOY_TRANSPORT_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "toy_audio_port.h"

typedef void (*toy_downlink_callback_t)(const uint8_t *pcm, size_t bytes,
                                        bool end_of_response, void *context);

typedef struct {
    int (*open)(void *context, const toy_pcm_format_t *capture_format,
                toy_downlink_callback_t on_audio, void *audio_context);
    /* Consume or copy pcm before returning; an empty final frame ends speech. */
    int (*send_audio)(void *context, const uint8_t *pcm, size_t bytes,
                      bool end_of_utterance);
    void (*close)(void *context);
} toy_transport_port_t;

#endif
