#include <string.h>

#include "frame.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc16_ccitt_false_check_value(void)
{
    /* The standard check value for CRC-16/CCITT-FALSE. */
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt((const uint8_t *)"123456789", 9));
}

static void test_crc16_of_nothing_is_the_initial_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_ccitt(NULL, 0));
}

static void test_frame_layout(void)
{
    uint8_t buf[64];
    const uint8_t payload[3] = {0x11, 0x22, 0x33};
    size_t n = frame_build(buf, FRAME_SAMPLES, 0x1234, payload, sizeof payload);

    TEST_ASSERT_EQUAL_UINT(FRAME_HDR_SIZE + 3 + FRAME_CRC_SIZE, n);
    const uint8_t hdr[] = {FRAME_SYNC, FRAME_SAMPLES, 0x34, 0x12, 0x03, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hdr, buf, sizeof hdr);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, buf + FRAME_HDR_SIZE, 3);

    uint16_t crc = crc16_ccitt(buf, FRAME_HDR_SIZE + 3);
    TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, buf[n - 2]);
    TEST_ASSERT_EQUAL_HEX8(crc >> 8, buf[n - 1]);
}

static void test_empty_payload(void)
{
    uint8_t buf[16];
    size_t n = frame_build(buf, FRAME_TEXT, 0, "", 0);
    TEST_ASSERT_EQUAL_UINT(FRAME_HDR_SIZE + FRAME_CRC_SIZE, n);
    TEST_ASSERT_EQUAL_HEX8(0, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0, buf[5]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_ccitt_false_check_value);
    RUN_TEST(test_crc16_of_nothing_is_the_initial_value);
    RUN_TEST(test_frame_layout);
    RUN_TEST(test_empty_payload);
    return UNITY_END();
}
