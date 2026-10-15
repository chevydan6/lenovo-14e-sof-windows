/* SPDX-License-Identifier: MIT */
#ifndef CHROMEBOOK_DSP_H
#define CHROMEBOOK_DSP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CB_SAMPLE_RATE 48000u
#define CB_FILTERS 14u
#define CB_BANDS 3u
#define CB_MAX_LOOKAHEAD 480u

typedef struct {
    double threshold_db, ratio, knee_db, attack_ms, release_ms, makeup_db;
} cb_compressor_config;

typedef struct {
    /* Static calibration is valid only at 48 kHz, stereo. */
    unsigned sample_rate;
    int equalizer_enabled, compressor_enabled, limiter_enabled;
    double output_gain_db;
    double crossover_low_hz, crossover_high_hz;
    cb_compressor_config bands[CB_BANDS];
    double limiter_ceiling_db, limiter_release_ms;
    unsigned lookahead_frames;
    /* Experimental measured magnitude correction, disabled by default. */
    int measured_bass_enabled;
    double input_gain_db;
    int harmonics_enabled;
} cb_config;

typedef struct { double b[3], a[2], z[2]; } cb_biquad;
typedef struct {
    double gain_reduction_db[CB_BANDS], limiter_gain_db, peak_output;
    uint64_t input_frames, invalid_samples;
} cb_meter;

/* Caller-owned state: no allocation, locks or I/O during processing.
 * One state per stream; a state must not be shared between processing threads.
 * Configure/reset while processing is stopped. Input and output may coincide.
 */
typedef struct {
    cb_config config;
    cb_biquad eq[2][CB_FILTERS];
    cb_biquad measured_bass[2][4];
    /* LR4 low/high branches at each crossover, plus low-band phase alignment. */
    cb_biquad split[2][12];
    double compressor_gain_db[CB_BANDS], attack[CB_BANDS], release[CB_BANDS];
    double harmonic_lowpass[2], harmonic_input_power[2], harmonic_filtered_power[2];
    double harmonic_alpha, harmonic_smoothing;
    double input_gain, output_gain, ceiling, limiter_release, limiter_gain;
    double delay[CB_MAX_LOOKAHEAD + 1][2];
    double queue_peak[CB_MAX_LOOKAHEAD + 2];
    uint64_t queue_frame[CB_MAX_LOOKAHEAD + 2], frame;
    unsigned queue_head, queue_count;
    cb_meter meter;
} cb_state;

void cb_default_config(cb_config *config);
size_t cb_state_size(void);
/* Experimental measured preset, volume 25..100; leaves config unchanged on error. */
int cb_measured_config(cb_config *config, double volume_percent);
/* 0 success, -1 invalid configuration. Failure leaves state unchanged. */
int cb_init(cb_state *state, const cb_config *config);
void cb_reset(cb_state *state);
unsigned cb_latency_frames(const cb_state *state);
void cb_process_f32(cb_state *state, const float *input, float *output, size_t frames);
void cb_process_s32(cb_state *state, const int32_t *input, int32_t *output, size_t frames);
void cb_get_meter(const cb_state *state, cb_meter *meter);
/* Steady-state linked feed-forward compressor curve; gain in decibels. */
double cb_compressor_curve(double input_db, const cb_compressor_config *config);

#ifdef __cplusplus
}
#endif
#endif
