#include <iostream>
#include "flight_recorder/FlightRecorder.h"

int main() {
    using namespace flight_recorder;
    calibrate();
    enable();
    setCategoryMask(0xFFFFFFFFu);

    for (int i = 0; i < 5; ++i) {
        FLIGHT_RECORDER_SPAN(Site::WorkerAppend, static_cast<uint32_t>(i + 1));
    }
    FLIGHT_RECORDER_MARK(Site::IngressRecv, 42);

    dumpHistograms(std::cout);
    if (!exportChromeTrace("/tmp/flight_recorder_sample_trace.json")) {
        std::cerr << "trace export failed\n";
        return 1;
    }
    std::cout << "wrote /tmp/flight_recorder_sample_trace.json\n";
    return 0;
}
