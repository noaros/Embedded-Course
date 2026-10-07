/*
 * Decimating FIR filter: 32 taps, decimation by 16, Q15 coefficients.
 * Pure C with no hardware dependencies (Lab 11 unit-tests it on the host).
 */
#ifndef FIR_H
#define FIR_H

#include <stddef.h>
#include <stdint.h>

#define FIR_TAPS 32u
#define FIR_DECIM 16u

struct fir_decim {
    int16_t hist[FIR_TAPS - 1]; /* the last TAPS-1 inputs of the previous block, oldest first */
};

void fir_decim_init(struct fir_decim *f);

/*
 * Filters n unsigned ADC samples (0..65535, mid-scale 32768) and writes
 * n / FIR_DECIM signed outputs. n must be a multiple of FIR_DECIM.
 * Returns the number of outputs written.
 */
size_t fir_decim_process(struct fir_decim *f, const uint16_t *in, size_t n, int16_t *out);

#endif
