#pragma once

// FLIGHT_RECORDER master switch. The build defines it to 1 (ON) or 0 (OFF).
// If a consumer includes headers without the define, default to enabled.
#ifndef FLIGHT_RECORDER
#define FLIGHT_RECORDER 1
#endif

// Platform detection for the timestamp source.
#if defined(__x86_64__) || defined(_M_X64)
#define FLIGHT_RECORDER_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define FLIGHT_RECORDER_ARM 1
#else
#define FLIGHT_RECORDER_FALLBACK 1
#endif

#define FLIGHT_RECORDER_CONCAT_INNER(a, b) a##b
#define FLIGHT_RECORDER_CONCAT(a, b) FLIGHT_RECORDER_CONCAT_INNER(a, b)
