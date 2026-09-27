// Compiled with RUNE_FR=0. Proves Event/macros are no-ops and emit no recorder
// symbols. This TU links ONLY against gtest (not rune_fr) — so if the no-op
// Event.h accidentally referenced threadRecorder()/rdtscp(), the link would fail
// and catch the regression.
#include <gtest/gtest.h>
#include "rune_fr/Event.h"

TEST(CompileOut, MacrosAndEventAreNoOps) {
    rune_fr::Event ev(rune_fr::Site::WorkerAppend);
    ev.begin().corr(1).arg(2);
    ev.commit();
    RUNE_FR_SPAN(rune_fr::Site::EgressSend, 3);
    RUNE_FR_MARK(rune_fr::Site::IngressRecv, 4);
    SUCCEED(); // compiling + linking with RUNE_FR=0 is the assertion
}
