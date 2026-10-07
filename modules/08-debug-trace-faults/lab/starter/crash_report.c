/*
 * Next-boot side of the crash dump: validate, decode and print the record.
 * (Complete: the capture side, crash.c, is yours to write.)
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "crash.h"

extern struct crash_record g_crash;

uint32_t crash_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static const char *const k_cfsr_names[32] = {
    [0] = "IACCVIOL", [1] = "DACCVIOL", [3] = "MUNSTKERR", [4] = "MSTKERR",
    [5] = "MLSPERR", [7] = "MMARVALID", [8] = "IBUSERR", [9] = "PRECISERR",
    [10] = "IMPRECISERR", [11] = "UNSTKERR", [12] = "STKERR", [13] = "LSPERR",
    [15] = "BFARVALID", [16] = "UNDEFINSTR", [17] = "INVSTATE", [18] = "INVPC",
    [19] = "NOCP", [24] = "UNALIGNED", [25] = "DIVBYZERO",
};

static const char *handler_name(uint32_t ipsr)
{
    switch (ipsr) {
    case 3: return "HardFault";
    case 4: return "MemManage";
    case 5: return "BusFault";
    case 6: return "UsageFault";
    default: return "?";
    }
}

void crash_report_if_any(void)
{
    const struct crash_record *r = &g_crash;
    if (r->magic != CRASH_MAGIC) {
        return;
    }
    if (crash_crc32(r, offsetof(struct crash_record, crc)) != r->crc) {
        printf("\n*** crash record present but CRC is wrong: ignoring it\n");
        memset(&g_crash, 0, sizeof g_crash);
        return;
    }

    printf("\n*** CRASH REPORT from the previous run ***\n");
    printf("handler   %s (IPSR %lu), EXC_RETURN 0x%08lx, SP 0x%08lx (%s)\n",
           handler_name(r->ipsr), (unsigned long)r->ipsr, (unsigned long)r->exc_return,
           (unsigned long)r->sp, (r->exc_return & 4u) ? "PSP" : "MSP");
    printf("PC 0x%08lx  LR 0x%08lx  xPSR 0x%08lx\n", (unsigned long)r->pc,
           (unsigned long)r->lr, (unsigned long)r->xpsr);
    printf("R0 0x%08lx  R1 0x%08lx  R2 0x%08lx  R3 0x%08lx  R12 0x%08lx\n",
           (unsigned long)r->r0, (unsigned long)r->r1, (unsigned long)r->r2,
           (unsigned long)r->r3, (unsigned long)r->r12);
    printf("CFSR 0x%08lx:", (unsigned long)r->cfsr);
    for (int b = 0; b < 32; b++) {
        if ((r->cfsr & (1u << b)) && k_cfsr_names[b]) {
            printf(" %s", k_cfsr_names[b]);
        }
    }
    printf("\nHFSR 0x%08lx%s%s\n", (unsigned long)r->hfsr,
           (r->hfsr & SCB_HFSR_FORCED_Msk) ? " FORCED" : "",
           (r->hfsr & SCB_HFSR_VECTTBL_Msk) ? " VECTTBL" : "");
    if (r->cfsr & SCB_CFSR_MMARVALID_Msk) {
        printf("MMFAR 0x%08lx\n", (unsigned long)r->mmfar);
    }
    if (r->cfsr & SCB_CFSR_BFARVALID_Msk) {
        printf("BFAR  0x%08lx\n", (unsigned long)r->bfar);
    }
    printf("stack:");
    for (unsigned i = 0; i < CRASH_STACK_WORDS; i++) {
        printf("%s%08lx", (i % 8) ? " " : "\n  ", (unsigned long)r->stack[i]);
    }
    printf("\nDecode with:\n  arm-none-eabi-addr2line -f -e <this image>.elf 0x%08lx 0x%08lx\n\n",
           (unsigned long)r->pc, (unsigned long)r->lr);

    memset(&g_crash, 0, sizeof g_crash);
}
