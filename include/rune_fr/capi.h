#pragma once
// C API for instrumenting C code (the Aeron C media driver) with rune-fr.
// Aeron declares these WEAK, so it still links without rune-fr.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum rune_fr_c_site {
    RUNE_FR_C_AERON_CONDUCTOR_DUTY = 0,
    RUNE_FR_C_AERON_SENDER_DUTY = 1,
    RUNE_FR_C_AERON_RECEIVER_DUTY = 2,
    RUNE_FR_C_AERON_SEND_DATA = 3,
    RUNE_FR_C_AERON_RECV_INSERT = 4,
    RUNE_FR_C_AERON_SM_SENT = 5,
    RUNE_FR_C_AERON_SM_RECV = 6,
    RUNE_FR_C_AERON_SEND_SYSCALL = 7,
    RUNE_FR_C_AERON_RECV_SYSCALL = 8,
    RUNE_FR_C_AERON_SENDER_DO_SEND = 9,
    RUNE_FR_C_AERON_SENDER_CONTROL_POLL = 10,
    RUNE_FR_C_AERON_PUB_SEND = 11,
    RUNE_FR_C_AERON_SEND_BLOCKED = 12,
    RUNE_FR_C_AERON_HEARTBEAT_SENT = 13,
    RUNE_FR_C_AERON_SETUP_SENT = 14,
    RUNE_FR_C_AERON_RETRANSMIT = 15,
    RUNE_FR_C_AERON_NAK_RECV = 16,
    RUNE_FR_C_AERON_NAK_SENT = 17,
    RUNE_FR_C_AERON_LOSS_GAP = 18,
    RUNE_FR_C_AERON_RECEIVER_POLL = 19,
    RUNE_FR_C_AERON_IMAGE_CLEAN = 20,
    RUNE_FR_C_AERON_PUB_CLEAN = 21,
    RUNE_FR_C_AERON_CONDUCTOR_CMD = 22,
    RUNE_FR_C_AERON_RECV_DISPATCH = 23,
    RUNE_FR_C_SITE_COUNT = 24
};
uint64_t rune_fr_c_now(void);
void     rune_fr_c_span(uint16_t c_site, uint32_t corr, uint64_t start_tsc, double value);
void     rune_fr_c_mark(uint16_t c_site, uint32_t corr, double value);
#ifdef __cplusplus
}
#endif
