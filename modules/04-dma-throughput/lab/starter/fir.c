#include "fir.h"

#include <string.h>

/* Hamming-windowed sinc, cutoff 0.025 fs (50 kHz at 2 MSPS), unity DC gain:
 * the taps sum to exactly 32768. Every tap is positive, so with |x| <= 32768
 * the accumulator stays below 2^30 and int32_t cannot overflow. */
static const int16_t k_taps[FIR_TAPS] = {
    49,   68,   107,  174,  273,  408,  578,  780,  1006, 1246, 1488,
    1718, 1923, 2090, 2208, 2268, 2268, 2208, 2090, 1923, 1718, 1488,
    1246, 1006, 780,  578,  408,  273,  174,  107,  68,   49,
};

void fir_decim_init(struct fir_decim *f)
{
    memset(f->hist, 0, sizeof f->hist);
}

/* Input sample i of the current block; negative i reaches into the history. */
static inline int32_t sample_at(const struct fir_decim *f, const uint16_t *in, long i)
{
    if (i >= 0) {
        return (int32_t)in[i] - 32768;
    }
    return f->hist[(long)(FIR_TAPS - 1) + i];
}

size_t fir_decim_process(struct fir_decim *f, const uint16_t *in, size_t n, int16_t *out)
{
    size_t outputs = n / FIR_DECIM;

    for (size_t k = 0; k < outputs; k++) {
        long newest = (long)((k + 1) * FIR_DECIM) - 1;
        int32_t acc = 0;
        for (size_t j = 0; j < FIR_TAPS; j++) {
            acc += (int32_t)k_taps[j] * sample_at(f, in, newest - (long)j);
        }
        /* Round and scale back from Q15. */
        out[k] = (int16_t)((acc + (1 << 14)) >> 15);
    }

    /* Keep the last TAPS-1 inputs for the next block. */
    if (n >= FIR_TAPS - 1) {
        for (size_t i = 0; i < FIR_TAPS - 1; i++) {
            f->hist[i] = (int16_t)((int32_t)in[n - (FIR_TAPS - 1) + i] - 32768);
        }
    } else {
        memmove(f->hist, f->hist + n, (FIR_TAPS - 1 - n) * sizeof f->hist[0]);
        for (size_t i = 0; i < n; i++) {
            f->hist[FIR_TAPS - 1 - n + i] = (int16_t)((int32_t)in[i] - 32768);
        }
    }
    return outputs;
}
