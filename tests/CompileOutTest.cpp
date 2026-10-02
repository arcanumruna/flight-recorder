// Compiled with RUNE_FR=0. Proves Event/macros are no-ops and emit no recorder
// symbols. This TU links ONLY against gtest (not rune_fr) — so if the no-op
// Event.h accidentally referenced threadRecorder()/rdtscp(), the link would fail
// and catch the regression.
#include <gtest/gtest.h>
#include "rune_fr/Categories.h"
#include "rune_fr/Event.h"
#include "rune_fr/Keys.h"

TEST(CompileOut, MacrosAndEventAreNoOps) {
    rune_fr::Event ev(rune_fr::Site::WorkerAppend);
    ev.begin().corr(1).arg(2);
    ev.commit();
    RUNE_FR_SPAN(rune_fr::Site::EgressSend, 3);
    RUNE_FR_MARK(rune_fr::Site::IngressRecv, 4);
    SUCCEED(); // compiling + linking with RUNE_FR=0 is the assertion
}

TEST(CompileOut, HopMacrosThrottleAndSlowSpanAreNoOps) {
    rune_fr::Throttle thr;
    RUNE_FR_SLOW_SPAN_RUNTIME(rune_fr::Site::ShardPass, 1);
    RUNE_FR_MARK_ARG_VAL_PRODUCE(rune_fr::Site::ShIngressFrame, rune_fr::foldCorr(7), 2, 3.0);
    RUNE_FR_MARK_VAL_STORAGE(rune_fr::Site::StorageFsyncDone, 4, 0.0);
    RUNE_FR_THROTTLED_REPLICATION(thr, rune_fr::Site::ShPushRefused, 5, 6);
    thr.hit(rune_fr::Site::ClientWindowFull, 1, 0);
    static_assert(sizeof(rune_fr::Throttle) == 1, "compiled-out Throttle carries no state");
    SUCCEED();
}
