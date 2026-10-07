#include <stdlib.h>
#include <string.h>

#include "fir.h"
#include "unity.h"

static struct fir_decim f;

void setUp(void) { fir_decim_init(&f); }
void tearDown(void) {}

static void fill(uint16_t *buf, size_t n, uint16_t v)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = v;
    }
}

static void test_output_count_is_input_over_decimation(void)
{
    uint16_t in[1024];
    int16_t out[64];
    fill(in, 1024, 32768);
    TEST_ASSERT_EQUAL_UINT(64, fir_decim_process(&f, in, 1024, out));
    TEST_ASSERT_EQUAL_UINT(1, fir_decim_process(&f, in, 16, out));
}

static void test_mid_scale_input_gives_zero(void)
{
    uint16_t in[256];
    int16_t out[16];
    fill(in, 256, 32768);
    fir_decim_process(&f, in, 256, out);
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(0, out[i]);
    }
}

static void test_dc_gain_is_unity_after_settling(void)
{
    uint16_t in[256];
    int16_t out[16];
    fill(in, 256, 32768 + 1000);
    fir_decim_process(&f, in, 256, out);
    /* The first output still sees the zero history; later ones are settled. */
    TEST_ASSERT_EQUAL_INT16(1000, out[15]);
}

static void test_full_scale_does_not_overflow(void)
{
    uint16_t in[256];
    int16_t out[16];
    fill(in, 256, 65535);
    fir_decim_process(&f, in, 256, out);
    TEST_ASSERT_EQUAL_INT16(32767, out[15]);
    fir_decim_init(&f);
    fill(in, 256, 0);
    fir_decim_process(&f, in, 256, out);
    TEST_ASSERT_EQUAL_INT16(-32768, out[15]);
}

static void test_nyquist_tone_is_attenuated(void)
{
    uint16_t in[512];
    int16_t out[32];
    for (int i = 0; i < 512; i++) {
        in[i] = (i & 1) ? 65535 : 0;
    }
    fir_decim_process(&f, in, 512, out);
    for (int i = 2; i < 32; i++) {
        TEST_ASSERT_INT16_WITHIN(64, 0, out[i]);
    }
}

/* Splitting a stream into blocks must not change the output: the history
 * carries the last 31 samples across calls. 16-sample blocks are shorter
 * than the history, which exercises the other branch. */
static void check_block_size_invariance(size_t block)
{
    enum { N = 2048 };
    static uint16_t in[N];
    static int16_t ref[N / FIR_DECIM], got[N / FIR_DECIM];
    srand(1234);
    for (int i = 0; i < N; i++) {
        in[i] = (uint16_t)rand();
    }
    struct fir_decim a, b;
    fir_decim_init(&a);
    fir_decim_init(&b);
    fir_decim_process(&a, in, N, ref);
    for (size_t off = 0; off < N; off += block) {
        fir_decim_process(&b, in + off, block, got + off / FIR_DECIM);
    }
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref, got, N / FIR_DECIM);
}

static void test_split_into_1024_blocks_matches_one_block(void) { check_block_size_invariance(1024); }
static void test_split_into_16_blocks_matches_one_block(void) { check_block_size_invariance(16); }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_output_count_is_input_over_decimation);
    RUN_TEST(test_mid_scale_input_gives_zero);
    RUN_TEST(test_dc_gain_is_unity_after_settling);
    RUN_TEST(test_full_scale_does_not_overflow);
    RUN_TEST(test_nyquist_tone_is_attenuated);
    RUN_TEST(test_split_into_1024_blocks_matches_one_block);
    RUN_TEST(test_split_into_16_blocks_matches_one_block);
    return UNITY_END();
}
