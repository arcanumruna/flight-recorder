#pragma once

#include <cstdint>

namespace rune_fr {

enum class Category : uint8_t {
    Produce,
    Replication,
    Read,
    Consensus,
    Storage,
    Egress,
    Count
};

// Static site registry — ONE source of truth. Hot path carries only the id.
enum class Site : uint16_t {
    None = 0,
    // --- produce path (Category::Produce) ---
    IngressRecv,
    GatewayHandle,
    WorkerEnqueue,
    WorkerAppend,
    StorageWrite,
    EgressClaim,
    EgressSend,
    ClientAckRecv,
    // ... grows as more paths are instrumented ...
    Count
};

struct SiteMeta {
    const char* name;
    Category category;
};

// Indexed by Site value. MUST stay in lockstep with the Site enum order.
inline constexpr SiteMeta kSiteMeta[] = {
    {"None", Category::Produce},
    {"IngressRecv", Category::Produce},
    {"GatewayHandle", Category::Produce},
    {"WorkerEnqueue", Category::Produce},
    {"WorkerAppend", Category::Produce},
    {"StorageWrite", Category::Storage},
    {"EgressClaim", Category::Egress},
    {"EgressSend", Category::Egress},
    {"ClientAckRecv", Category::Produce},
};

static_assert(sizeof(kSiteMeta) / sizeof(kSiteMeta[0]) ==
                  static_cast<size_t>(Site::Count),
              "kSiteMeta must have exactly one entry per Site (excluding Count)");

inline Category categoryOf(Site s) noexcept {
    return kSiteMeta[static_cast<uint16_t>(s)].category;
}

inline const char* nameOf(Site s) noexcept {
    return kSiteMeta[static_cast<uint16_t>(s)].name;
}

} // namespace rune_fr
