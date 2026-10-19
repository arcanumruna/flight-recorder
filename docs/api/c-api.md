# C API

A small C interface, `<flight_recorder/capi.h>`, lets C code (the Aeron C media driver) record into the same rings as C++
code. It has three functions and an enum of 24 Aeron driver sites. The C sites map one-to-one onto C++ `Site` values
(`AeronConductorDuty`, `AeronSendData`, ...), so the driver's spans appear next to the application's in every report.

```c
#include <flight_recorder/capi.h>

void send_one(uint32_t corr, double bytes) {
    uint64_t t0 = flight_recorder_c_now();                  /* 0 while the recorder is disabled: no clock read */
    /* ... the work being measured ... */
    flight_recorder_c_span(FLIGHT_RECORDER_C_AERON_SEND_DATA, corr, t0, bytes);
}

void on_nak(uint32_t corr) {
    flight_recorder_c_mark(FLIGHT_RECORDER_C_AERON_NAK_RECV, corr, 0.0);
}
```

| Function | Meaning |
|---|---|
| `uint64_t flight_recorder_c_now(void)` | The start timestamp for a span. Returns **0 while the recorder is disabled**, so a disabled hook costs no clock read. While enabled it reads the tick counter even for a site that is masked off (the end call filters). |
| `void flight_recorder_c_span(uint16_t c_site, uint32_t corr, uint64_t start_tsc, double value)` | Records a span from `start_tsc` to now. A span whose start is 0 records nothing, so one that started while disabled and ended after enabling is dropped, never recorded with a bogus duration. |
| `void flight_recorder_c_mark(uint16_t c_site, uint32_t corr, double value)` | Records an instant. |

A site is recorded only if the recorder is enabled, the C site's bit is set in the **Aeron mask** and the mapped `Site`'s
category is enabled (`FLIGHT_RECORDER_CATEGORIES`).

## Linking

The functions are defined in `src/Registry.cpp`, inside the library every consumer already links, so they always resolve.
Aeron declares them **weak**, so a build of Aeron without flight-recorder still links. With `FLIGHT_RECORDER=0` the
definitions are stubs (`flight_recorder_c_now` returns 0, the others do nothing).

## The Aeron mask

`FLIGHT_RECORDER_AERON_MASK` (hex or decimal, `strtoul` base 0) selects which C sites record; bit `i` is C site `i`. The default is
`0xC7F0F8`: the per-duty-cycle spans fire on every idle cycle and overflowed the per-thread rings (36% of records dropped),
hiding the hop events, so the default keeps only the sites that explain a hop. `0xFFFFFF` records everything.

| Bit | C site | Maps to `Site` | In the default mask |
|---|---|---|---|
| 0 | `FLIGHT_RECORDER_C_AERON_CONDUCTOR_DUTY` | `AeronConductorDuty` | no |
| 1 | `FLIGHT_RECORDER_C_AERON_SENDER_DUTY` | `AeronSenderDuty` | no |
| 2 | `FLIGHT_RECORDER_C_AERON_RECEIVER_DUTY` | `AeronReceiverDuty` | no |
| 3 | `FLIGHT_RECORDER_C_AERON_SEND_DATA` | `AeronSendData` | yes |
| 4 | `FLIGHT_RECORDER_C_AERON_RECV_INSERT` | `AeronRecvInsert` | yes |
| 5 | `FLIGHT_RECORDER_C_AERON_SM_SENT` | `AeronSmSent` | yes |
| 6 | `FLIGHT_RECORDER_C_AERON_SM_RECV` | `AeronSmRecv` | yes |
| 7 | `FLIGHT_RECORDER_C_AERON_SEND_SYSCALL` | `AeronSendSyscall` | yes |
| 8 | `FLIGHT_RECORDER_C_AERON_RECV_SYSCALL` | `AeronRecvSyscall` | no |
| 9 | `FLIGHT_RECORDER_C_AERON_SENDER_DO_SEND` | `AeronSenderDoSend` | no |
| 10 | `FLIGHT_RECORDER_C_AERON_SENDER_CONTROL_POLL` | `AeronSenderControlPoll` | no |
| 11 | `FLIGHT_RECORDER_C_AERON_PUB_SEND` | `AeronPubSend` | no |
| 12 | `FLIGHT_RECORDER_C_AERON_SEND_BLOCKED` | `AeronSendBlocked` | yes |
| 13 | `FLIGHT_RECORDER_C_AERON_HEARTBEAT_SENT` | `AeronHeartbeatSent` | yes |
| 14 | `FLIGHT_RECORDER_C_AERON_SETUP_SENT` | `AeronSetupSent` | yes |
| 15 | `FLIGHT_RECORDER_C_AERON_RETRANSMIT` | `AeronRetransmit` | yes |
| 16 | `FLIGHT_RECORDER_C_AERON_NAK_RECV` | `AeronNakRecv` | yes |
| 17 | `FLIGHT_RECORDER_C_AERON_NAK_SENT` | `AeronNakSent` | yes |
| 18 | `FLIGHT_RECORDER_C_AERON_LOSS_GAP` | `AeronLossGap` | yes |
| 19 | `FLIGHT_RECORDER_C_AERON_RECEIVER_POLL` | `AeronReceiverPoll` | no |
| 20 | `FLIGHT_RECORDER_C_AERON_IMAGE_CLEAN` | `AeronImageClean` | no |
| 21 | `FLIGHT_RECORDER_C_AERON_PUB_CLEAN` | `AeronPubClean` | no |
| 22 | `FLIGHT_RECORDER_C_AERON_CONDUCTOR_CMD` | `AeronConductorCmd` | yes |
| 23 | `FLIGHT_RECORDER_C_AERON_RECV_DISPATCH` | `AeronRecvDispatch` | yes |

The mask is read once, at the first C call.
