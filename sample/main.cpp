#include <iostream>
#include "rune_fr/FlightRecorder.h"

int main() {
    using namespace rune_fr;
    calibrate();
    enable();
    setCategoryMask(0xFFFFFFFFu);

    for (int i = 0; i < 5; ++i) {
        RUNE_FR_SPAN(Site::WorkerAppend, static_cast<uint32_t>(i + 1));
    }
    RUNE_FR_MARK(Site::IngressRecv, 42);

    dumpHistograms(std::cout);
    if (!exportChromeTrace("/tmp/rune_fr_sample_trace.json")) {
        std::cerr << "trace export failed\n";
        return 1;
    }
    std::cout << "wrote /tmp/rune_fr_sample_trace.json\n";
    return 0;
}
