#ifndef TOY_AUDIO_PORT_H
#define TOY_AUDIO_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t channels;
    uint8_t bits_per_sample;
} toy_pcm_format_t;

typedef struct {
    toy_pcm_format_t capture;
    toy_pcm_format_t playback;
    bool request_aec;
} toy_audio_config_t;

typedef void (*toy_capture_callback_t)(const uint8_t *pcm, size_t bytes,
                                       void *context);

typedef struct {
    /* Return nonzero if requested AEC cannot be activated. */
    int (*open)(void *context, const toy_audio_config_t *config,
                toy_capture_callback_t on_capture, void *capture_context);
    int (*start_capture)(void *context);
    int (*stop_capture)(void *context);
    /* Consume or copy pcm before returning. Capture and playback may overlap. */
    int (*play_pcm)(void *context, const uint8_t *pcm, size_t bytes);
    bool (*aec_active)(void *context);
    void (*close)(void *context);
} toy_audio_port_t;

#endif
