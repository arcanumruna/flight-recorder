#pragma once

// RUNE_FR master switch. The build defines it to 1 (ON) or 0 (OFF).
// If a consumer includes headers without the define, default to enabled.
#ifndef RUNE_FR
#define RUNE_FR 1
#endif

// Platform detection for the timestamp source.
#if defined(__x86_64__) || defined(_M_X64)
#define RUNE_FR_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define RUNE_FR_ARM 1
#else
#define RUNE_FR_FALLBACK 1
#endif

#define RUNE_FR_CONCAT_INNER(a, b) a##b
#define RUNE_FR_CONCAT(a, b) RUNE_FR_CONCAT_INNER(a, b)
