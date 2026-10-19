#pragma once
// C API for instrumenting C code (the Aeron C media driver) with flight-recorder.
// Aeron declares these WEAK, so it still links without flight-recorder.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum flight_recorder_c_site {
    FLIGHT_RECORDER_C_AERON_CONDUCTOR_DUTY = 0,
    FLIGHT_RECORDER_C_AERON_SENDER_DUTY = 1,
    FLIGHT_RECORDER_C_AERON_RECEIVER_DUTY = 2,
    FLIGHT_RECORDER_C_AERON_SEND_DATA = 3,
    FLIGHT_RECORDER_C_AERON_RECV_INSERT = 4,
    FLIGHT_RECORDER_C_AERON_SM_SENT = 5,
    FLIGHT_RECORDER_C_AERON_SM_RECV = 6,
    FLIGHT_RECORDER_C_AERON_SEND_SYSCALL = 7,
    FLIGHT_RECORDER_C_AERON_RECV_SYSCALL = 8,
    FLIGHT_RECORDER_C_AERON_SENDER_DO_SEND = 9,
    FLIGHT_RECORDER_C_AERON_SENDER_CONTROL_POLL = 10,
    FLIGHT_RECORDER_C_AERON_PUB_SEND = 11,
    FLIGHT_RECORDER_C_AERON_SEND_BLOCKED = 12,
    FLIGHT_RECORDER_C_AERON_HEARTBEAT_SENT = 13,
    FLIGHT_RECORDER_C_AERON_SETUP_SENT = 14,
    FLIGHT_RECORDER_C_AERON_RETRANSMIT = 15,
    FLIGHT_RECORDER_C_AERON_NAK_RECV = 16,
    FLIGHT_RECORDER_C_AERON_NAK_SENT = 17,
    FLIGHT_RECORDER_C_AERON_LOSS_GAP = 18,
    FLIGHT_RECORDER_C_AERON_RECEIVER_POLL = 19,
    FLIGHT_RECORDER_C_AERON_IMAGE_CLEAN = 20,
    FLIGHT_RECORDER_C_AERON_PUB_CLEAN = 21,
    FLIGHT_RECORDER_C_AERON_CONDUCTOR_CMD = 22,
    FLIGHT_RECORDER_C_AERON_RECV_DISPATCH = 23,
    FLIGHT_RECORDER_C_SITE_COUNT = 24
};
/* Start timestamp for flight_recorder_c_span.  Returns 0 while the recorder is disabled
 * (no TSC read on the hot path); a span whose start is 0 records nothing, so a
 * span started while disabled and ended after enabling is dropped, never
 * recorded with a bogus duration.  While enabled it reads the TSC even for a site
 * masked off by FLIGHT_RECORDER_AERON_MASK (now() takes no site; the end call filters). */
uint64_t flight_recorder_c_now(void);
void     flight_recorder_c_span(uint16_t c_site, uint32_t corr, uint64_t start_tsc, double value);
void     flight_recorder_c_mark(uint16_t c_site, uint32_t corr, double value);
#ifdef __cplusplus
}
#endif
