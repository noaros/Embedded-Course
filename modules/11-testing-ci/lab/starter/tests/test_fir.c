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

/* TODO: add tests for
 *   - mid-scale input (32768) gives exactly 0
 *   - DC gain: a constant offset comes out unchanged once the filter has settled
 *   - full-scale inputs (0 and 65535) don't overflow
 *   - a Nyquist tone (alternating 0/65535) is strongly attenuated
 *   - splitting a stream into blocks (1024, and 16 < history length) gives
 *     exactly the same output as one big block
 * Aim for 100% line coverage of fir.c. */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_output_count_is_input_over_decimation);
    return UNITY_END();
}
