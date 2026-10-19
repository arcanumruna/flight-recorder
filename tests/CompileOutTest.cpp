// Compiled with FLIGHT_RECORDER=0. Proves Event/macros are no-ops and emit no recorder
// symbols. This TU links ONLY against gtest (not flight_recorder) — so if the no-op
// Event.h accidentally referenced threadRecorder()/rdtscp(), the link would fail
// and catch the regression.
#include <gtest/gtest.h>
#include "flight_recorder/Categories.h"
#include "flight_recorder/Event.h"
#include "flight_recorder/Keys.h"

TEST(CompileOut, MacrosAndEventAreNoOps) {
    flight_recorder::Event ev(flight_recorder::Site::WorkerAppend);
    ev.begin().corr(1).arg(2);
    ev.commit();
    FLIGHT_RECORDER_SPAN(flight_recorder::Site::EgressSend, 3);
    FLIGHT_RECORDER_MARK(flight_recorder::Site::IngressRecv, 4);
    SUCCEED(); // compiling + linking with FLIGHT_RECORDER=0 is the assertion
}

TEST(CompileOut, HopMacrosThrottleAndSlowSpanAreNoOps) {
    flight_recorder::Throttle thr;
    FLIGHT_RECORDER_SLOW_SPAN_RUNTIME(flight_recorder::Site::ShardPass, 1);
    FLIGHT_RECORDER_MARK_ARG_VAL_PRODUCE(flight_recorder::Site::ShIngressFrame, flight_recorder::foldCorr(7), 2, 3.0);
    FLIGHT_RECORDER_MARK_VAL_STORAGE(flight_recorder::Site::StorageFsyncDone, 4, 0.0);
    FLIGHT_RECORDER_THROTTLED_REPLICATION(thr, flight_recorder::Site::ShPushRefused, 5, 6);
    thr.hit(flight_recorder::Site::ClientWindowFull, 1, 0);
    static_assert(sizeof(flight_recorder::Throttle) == 1, "compiled-out Throttle carries no state");
    SUCCEED();
}
