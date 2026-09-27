#pragma once

#include <cstddef>
#include <cstdint>

namespace rune_fr {

enum class Category : uint8_t {
    Produce,
    Replication,
    Read,
    Consensus,
    Storage,
    Egress,
    Runtime,
    Count
};

// Static site registry — ONE source of truth. Hot path carries only the id.
// NOTE: kSiteMeta below MUST stay in lockstep with this enum (same order, one
// entry per value excluding Count) — the static_assert enforces the count.
enum class Site : uint16_t {
    None = 0,

    // --- produce path (Category::Produce) — original 8, order preserved ---
    IngressRecv,      // gateway ingress subscription delivered a client frame
    GatewayHandle,    // gateway handleProduce entry→exit
    WorkerEnqueue,    // gateway → owning worker tryEnqueueOp (produce op)
    WorkerAppend,     // worker applies produce op → append to log
    StorageWrite,     // storage append (pwritev) — Category::Storage
    EgressClaim,      // egress publication tryClaim — Category::Egress
    EgressSend,       // egress publication commit/offer — Category::Egress
    ClientAckRecv,    // (client-side) ack received — Category::Produce

    // --- produce path, finer detail (Category::Produce) ---
    GatewayProduceDecode, // decode the produce frame / record batch
    GatewayRouteProduce,  // routing-image lookup + leader/redirect decision
    WorkerProcessOp,      // worker dequeues + dispatches one WorkerOp
    ProduceAckEncode,     // encode ProduceResponse
    ProduceAckSend,       // sendRespReliable / enqueueOrBestEffort of the ack
    WorkerIngressRecv,    // produce frame received + handled at the worker ingress

    // --- storage (Category::Storage) ---
    StorageFlush,         // fsync / flush / durability barrier

    // --- egress (Category::Egress) ---
    EgressPush,           // worker read→encode→push to a consumer

    // --- replication (Category::Replication) ---
    ReplIngressPoll,      // AeronPartitionTransport::poll (controlledPoll)
    ReplOnFragment,       // onFragment dispatch of one inbound frame
    ReplFetchReqHandle,   // leader onFetchRequest (read log + build response)
    ReplFetchRespApply,   // follower applyReplicated
    ReplSendFetchResp,    // sendFetchResponse encode + enqueueOutbound
    ReplOutboundDrain,    // drainOutbound (control thread offers to peers)
    ReplEnqueueOutbound,  // enqueueOutbound (worker → outbound ring)
    ReplPushToFollowers,  // leader pushToFollowers after append (GR-10)
    ReplPendingAckEnqueue,// acks=all: produce parked in pendingAcksAll_ (mark, corr)
    ReplPendingAckFire,   // acks=all: HW reached → ack fired (mark, corr)

    // --- consensus (Category::Consensus) ---
    ConsensusDoWork,      // NodeConsensusAgent duty cycle
    ConsensusIngressPoll, // aeron ingress poll → IngressRing fill
    ElectionStep,         // Election::doWork

    // --- read / consume (Category::Read) ---
    ReadSubscribe,        // subscribe/unsubscribe handling
    ReadLogRead,          // zero-copy Log::readView
    ReadEncodePush,       // encode records + push to consumer egress

    // --- runtime (Category::Runtime) ---
    RuntimeDoWork,        // NodeRuntimeAgent duty cycle (one tick)
    MetadataDoWork,       // MetadataService::doWork
    WorkerDoWork,         // WorkerPool worker duty cycle (one tick) — inter-tick gap
                          // exposes scheduler handoff / park time on the worker thread

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

    // original 8
    {"IngressRecv", Category::Produce},
    {"GatewayHandle", Category::Produce},
    {"WorkerEnqueue", Category::Produce},
    {"WorkerAppend", Category::Produce},
    {"StorageWrite", Category::Storage},
    {"EgressClaim", Category::Egress},
    {"EgressSend", Category::Egress},
    {"ClientAckRecv", Category::Produce},

    // produce detail
    {"GatewayProduceDecode", Category::Produce},
    {"GatewayRouteProduce", Category::Produce},
    {"WorkerProcessOp", Category::Produce},
    {"ProduceAckEncode", Category::Produce},
    {"ProduceAckSend", Category::Produce},
    {"WorkerIngressRecv", Category::Produce},

    // storage
    {"StorageFlush", Category::Storage},

    // egress
    {"EgressPush", Category::Egress},

    // replication
    {"ReplIngressPoll", Category::Replication},
    {"ReplOnFragment", Category::Replication},
    {"ReplFetchReqHandle", Category::Replication},
    {"ReplFetchRespApply", Category::Replication},
    {"ReplSendFetchResp", Category::Replication},
    {"ReplOutboundDrain", Category::Replication},
    {"ReplEnqueueOutbound", Category::Replication},
    {"ReplPushToFollowers", Category::Replication},
    {"ReplPendingAckEnqueue", Category::Replication},
    {"ReplPendingAckFire", Category::Replication},

    // consensus
    {"ConsensusDoWork", Category::Consensus},
    {"ConsensusIngressPoll", Category::Consensus},
    {"ElectionStep", Category::Consensus},

    // read
    {"ReadSubscribe", Category::Read},
    {"ReadLogRead", Category::Read},
    {"ReadEncodePush", Category::Read},

    // runtime
    {"RuntimeDoWork", Category::Runtime},
    {"MetadataDoWork", Category::Runtime},
    {"WorkerDoWork", Category::Runtime},
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
