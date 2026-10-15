/* SPDX-License-Identifier: MIT */
#include "chromebook_dsp.h"
#include <math.h>
#include <string.h>
#include <limits.h>
#include "speaker_profile.h"
#include "measured_bass.h"
#include "measured_dynamics.h"
#include "measured_harmonics.h"

#define PI 3.14159265358979323846
#define QUEUE_SIZE (CB_MAX_LOOKAHEAD + 2u)
static double db_gain(double db) { return pow(10.0, db / 20.0); }
static double gain_db(double gain) { return 20.0 * log10(fmax(gain, 1e-30)); }
static int bounded(double x, double lo, double hi) { return isfinite(x) && x >= lo && x <= hi; }
static double smooth(double ms) { return exp(-1.0 / (0.001 * ms * CB_SAMPLE_RATE)); }
int cb_measured_config(cb_config *c, double volume) {
    cb_config preset;
    double attenuation;
    unsigned b;
    const double *v = measured_dynamics_parameters;
    if (!c || !isfinite(volume) || volume < 25 || volume > 100) return -1;
    if (volume <= 50) attenuation = -42.8 + (volume-25)*19.3/25;
    else if (volume <= 75) attenuation = -23.5 + (volume-50)*10/25;
    else attenuation = -13.5 + (volume-75)*4.5/25;
    cb_default_config(&preset);
    preset.harmonics_enabled = 1;
    preset.measured_bass_enabled = 1; preset.compressor_enabled = 1;
    preset.input_gain_db = attenuation + 23.5;
    preset.crossover_low_hz = 150; preset.crossover_high_hz = 500;
    for (b=0; b<3; ++b) {
        preset.bands[b].attack_ms = 5; preset.bands[b].release_ms = 100;
        if (b<2) {
            preset.bands[b].threshold_db = v[b*4]; preset.bands[b].ratio = v[b*4+1];
            preset.bands[b].knee_db = v[b*4+2]; preset.bands[b].makeup_db = v[b*4+3];
        } else { preset.bands[b].ratio = 1; preset.bands[b].makeup_db = v[8]; }
    }
    *c = preset; return 0;
}

size_t cb_state_size(void) { return sizeof(cb_state); }

static void set_sos(cb_biquad *s, const double *v) {
    memcpy(s->b, v, 3 * sizeof(double));
    memcpy(s->a, v + 3, 2 * sizeof(double));
    s->z[0] = s->z[1] = 0.0;
}
static double biquad(cb_biquad *s, double x) {
    double y = s->b[0] * x + s->z[0];
    s->z[0] = s->b[1] * x - s->a[0] * y + s->z[1];
    s->z[1] = s->b[2] * x - s->a[1] * y;
    if (fabs(s->z[0]) < 1e-25) s->z[0] = 0;
    if (fabs(s->z[1]) < 1e-25) s->z[1] = 0;
    return y;
}
static double cascade(cb_biquad *s, unsigned count, double x) {
    unsigned i;
    for (i = 0; i < count; ++i) x = biquad(&s[i], x);
    return x;
}
/* Two identical Butterworth second-order sections form each LR4 branch. */
static void crossover(cb_biquad *s, double hz, int highpass) {
    double w = 2 * PI * hz / CB_SAMPLE_RATE, c = cos(w), a = sin(w) / sqrt(2.0);
    double b0 = (1.0 + (highpass ? c : -c)) / 2.0;
    double v[5] = {b0 / (1+a), (highpass ? -2 : 2) * b0 / (1+a), b0 / (1+a),
                   -2*c / (1+a), (1-a) / (1+a)};
    set_sos(s, v); set_sos(s+1, v);
}

void cb_default_config(cb_config *c) {
    unsigned i;
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->sample_rate = CB_SAMPLE_RATE;
    c->equalizer_enabled = 1;
    c->output_gain_db = -9;
    /* These crossovers/dynamics are independent defaults, NOT recovered Waves values.
     * Compression is disabled unless the caller explicitly enables it. */
    c->crossover_low_hz = 200; c->crossover_high_hz = 2500;
    for (i=0; i<CB_BANDS; ++i) {
        c->bands[i].threshold_db = -18; c->bands[i].ratio = 2;
        c->bands[i].knee_db = 6; c->bands[i].attack_ms = 10;
        c->bands[i].release_ms = 150;
    }
    c->limiter_enabled = 1; c->limiter_ceiling_db = -1;
    c->limiter_release_ms = 80; c->lookahead_frames = 144;
}

int cb_init(cb_state *s, const cb_config *c) {
    unsigned ch, i;
    if (!s || !c || c->sample_rate != CB_SAMPLE_RATE ||
        !bounded(c->output_gain_db, -96, 12) || !bounded(c->input_gain_db, -96, 24) ||
        !bounded(c->crossover_low_hz, 20, 8000) ||
        !bounded(c->crossover_high_hz, c->crossover_low_hz * 1.5, 20000) ||
        !bounded(c->limiter_ceiling_db, -24, 0) ||
        !bounded(c->limiter_release_ms, 1, 5000) || c->lookahead_frames > CB_MAX_LOOKAHEAD)
        return -1;
    for (i=0; i<CB_BANDS; ++i) {
        const cb_compressor_config *b = &c->bands[i];
        if (!bounded(b->threshold_db, -96, 0) || !bounded(b->ratio, 1, 100) ||
            !bounded(b->knee_db, 0, 60) || !bounded(b->attack_ms, 0.01, 1000) ||
            !bounded(b->release_ms, 1, 5000) || !bounded(b->makeup_db, -24, 24)) return -1;
    }
    /* Copy config first: cb_reset may pass a config belonging to this state. */
    {
        cb_config copy = *c;
        memset(s, 0, sizeof(*s)); s->config = copy;
    }
    for (ch=0; ch<2; ++ch) {
        for (i=0; i<CB_FILTERS; ++i) set_sos(&s->eq[ch][i], speaker_sos[i]);
        for (i=0; i<4; ++i) set_sos(&s->measured_bass[ch][i], measured_bass_sos[i]);
        crossover(&s->split[ch][0], s->config.crossover_low_hz, 0);
        crossover(&s->split[ch][2], s->config.crossover_low_hz, 1);
        crossover(&s->split[ch][4], s->config.crossover_high_hz, 0);
        crossover(&s->split[ch][6], s->config.crossover_high_hz, 1);
        /* Match the high split's all-pass phase in the low-frequency branch. */
        crossover(&s->split[ch][8], s->config.crossover_high_hz, 0);
        crossover(&s->split[ch][10], s->config.crossover_high_hz, 1);
    }
    for (i=0; i<CB_BANDS; ++i) {
        s->attack[i] = smooth(s->config.bands[i].attack_ms);
        s->release[i] = smooth(s->config.bands[i].release_ms);
    }
    s->harmonic_alpha = 1-exp(-2*3.14159265358979323846*measured_harmonic_parameters[1]/CB_SAMPLE_RATE);
    s->harmonic_smoothing = smooth(30);
    s->input_gain = db_gain(s->config.input_gain_db);
    s->output_gain = db_gain(s->config.output_gain_db);
    s->ceiling = db_gain(s->config.limiter_ceiling_db);
    s->limiter_release = smooth(s->config.limiter_release_ms); s->limiter_gain = 1;
    return 0;
}
void cb_reset(cb_state *s) { if (s) { cb_config c=s->config; (void)cb_init(s, &c); } }
unsigned cb_latency_frames(const cb_state *s) { return s && s->config.limiter_enabled ? s->config.lookahead_frames : 0; }

double cb_compressor_curve(double x, const cb_compressor_config *c) {
    double d = x - c->threshold_db, slope = 1.0 / c->ratio - 1.0;
    if (c->knee_db > 0 && d > -c->knee_db/2 && d < c->knee_db/2) {
        double k = d + c->knee_db/2;
        return slope * k*k / (2*c->knee_db) + c->makeup_db;
    }
    return (d > 0 ? slope*d : 0) + c->makeup_db;
}

static void compress(cb_state *s, double x[2]) {
    double bands[CB_BANDS][2]; unsigned ch, b;
    for (ch=0; ch<2; ++ch) {
        double low=cascade(&s->split[ch][0], 2, x[ch]);
        double high=cascade(&s->split[ch][2], 2, x[ch]);
        bands[0][ch]=cascade(&s->split[ch][8], 2, low)+cascade(&s->split[ch][10], 2, low);
        bands[1][ch]=cascade(&s->split[ch][4], 2, high);
        bands[2][ch]=cascade(&s->split[ch][6], 2, high);
        x[ch]=0;
    }
    for (b=0; b<CB_BANDS; ++b) {
        const cb_compressor_config *c=&s->config.bands[b];
        double peak=fmax(fabs(bands[b][0]), fabs(bands[b][1]));
        double target=cb_compressor_curve(gain_db(peak), c)-c->makeup_db;
        double coefficient=target<s->compressor_gain_db[b] ? s->attack[b] : s->release[b];
        double gain;
        s->compressor_gain_db[b]=coefficient*s->compressor_gain_db[b]+(1-coefficient)*target;
        s->meter.gain_reduction_db[b]=-s->compressor_gain_db[b];
        gain=db_gain(s->compressor_gain_db[b]+c->makeup_db);
        for (ch=0; ch<2; ++ch) x[ch]+=bands[b][ch]*gain;
    }
}

static void limit(cb_state *s, double x[2]) {
    unsigned capacity=s->config.lookahead_frames+1, pos=(unsigned)(s->frame % capacity);
    double peak=fmax(fabs(x[0]), fabs(x[1])), delayed[2], target;
    uint64_t oldest=s->frame >= s->config.lookahead_frames ? s->frame-s->config.lookahead_frames : 0;
    unsigned tail;
    /* Inclusive window [n-lookahead,n]. The output sample is its oldest frame. */
    while (s->queue_count && s->queue_frame[s->queue_head]<oldest) {
        s->queue_head=(s->queue_head+1)%QUEUE_SIZE; --s->queue_count;
    }
    while (s->queue_count) {
        tail=(s->queue_head+s->queue_count-1)%QUEUE_SIZE;
        if (s->queue_peak[tail]>peak) break;
        --s->queue_count;
    }
    tail=(s->queue_head+s->queue_count)%QUEUE_SIZE;
    s->queue_peak[tail]=peak; s->queue_frame[tail]=s->frame; ++s->queue_count;
    s->delay[pos][0]=x[0]; s->delay[pos][1]=x[1];
    if (s->frame<s->config.lookahead_frames) delayed[0]=delayed[1]=0;
    else {
        unsigned read=(unsigned)(oldest % capacity);
        delayed[0]=s->delay[read][0]; delayed[1]=s->delay[read][1];
    }
    peak=s->queue_peak[s->queue_head]; target=peak>s->ceiling ? s->ceiling/peak : 1;
    if (target<s->limiter_gain) s->limiter_gain=target;
    else s->limiter_gain=fmin(target, s->limiter_release*s->limiter_gain+(1-s->limiter_release)*target);
    x[0]=delayed[0]*s->limiter_gain; x[1]=delayed[1]*s->limiter_gain;
    s->meter.limiter_gain_db=gain_db(s->limiter_gain); ++s->frame;
}

/* Normalized cubic branch. For a settled sinusoid the fundamental cancels,
 * leaving a third harmonic. On mixed signals this also creates intermodulation.
 * This is an independent approximation, not vendor code or a physical model. */
static double harmonic(cb_state *s, unsigned ch, double input) {
    const double *p = measured_harmonic_parameters;
    double a=s->harmonic_alpha, memory=s->harmonic_smoothing;
    double u, power, filtered, level, z, logden, strength, value;
    s->harmonic_lowpass[ch] += a*(input-s->harmonic_lowpass[ch]);
    u=s->harmonic_lowpass[ch];
    power=s->harmonic_input_power[ch]=memory*s->harmonic_input_power[ch]+(1-memory)*input*input;
    filtered=s->harmonic_filtered_power[ch]=memory*s->harmonic_filtered_power[ch]+(1-memory)*u*u;
    if (power < 1e-14) return 0;
    level=10*log10(2*power); z=(level-p[2])*log(10.0)/20;
    /* Stable log(1 + exp(p[4]*z)). */
    logden=fmax(0,p[4]*z)+log1p(exp(-fabs(p[4]*z)));
    strength=db_gain(p[0]+9+p[3]*(level-p[2])-20/log(10.0)*logden);
    value=(2*u*u*u-3*u*filtered)/power;
    return fmax(-16,fmin(16,value*strength));
}

static void process(cb_state *s, double x[2]) {
    unsigned ch;
    double extra[2]={0,0};
    for (ch=0; ch<2; ++ch) {
        if (!isfinite(x[ch])) {x[ch]=0; ++s->meter.invalid_samples;}
        /* Bounded audio input avoids state overflow for untrusted float WAV files. */
        x[ch]=fmax(-16, fmin(16, x[ch]));
        x[ch]*=s->input_gain;
        if (s->config.harmonics_enabled) extra[ch]=harmonic(s,ch,x[ch]);
        if (s->config.equalizer_enabled) x[ch]=cascade(s->eq[ch], CB_FILTERS, x[ch]);
        if (s->config.measured_bass_enabled) x[ch]=cascade(s->measured_bass[ch], 4, x[ch]);
    }
    if (s->config.compressor_enabled) compress(s, x);
    for (ch=0; ch<2; ++ch) x[ch]=(x[ch]+extra[ch])*s->output_gain;
    if (s->config.limiter_enabled) limit(s, x);
    for (ch=0; ch<2; ++ch) s->meter.peak_output=fmax(s->meter.peak_output, fabs(x[ch]));
    ++s->meter.input_frames;
}
void cb_process_f32(cb_state *s, const float *in, float *out, size_t n) {
    size_t i;
    if (!s || !in || !out) return;
    for (i=0; i<n; ++i) {
        double x[2]={in[2*i], in[2*i+1]};process(s,x);out[2*i]=(float)x[0];out[2*i+1]=(float)x[1];
    }
}
void cb_process_s32(cb_state *s, const int32_t *in, int32_t *out, size_t n) {
    size_t i; unsigned ch;
    if (!s || !in || !out) return;
    for (i=0; i<n; ++i) {
        double x[2]={in[2*i]/2147483648.0, in[2*i+1]/2147483648.0};process(s,x);
        for (ch=0; ch<2; ++ch) {
            double v=x[ch]*2147483648.0;
            out[2*i+ch]=v>=INT32_MAX ? INT32_MAX : v<=INT32_MIN ? INT32_MIN : (int32_t)v;
        }
    }
}
void cb_get_meter(const cb_state *s, cb_meter *m) { if (s && m) *m=s->meter; }
