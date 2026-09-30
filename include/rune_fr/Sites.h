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
    ReplLeaderAppend,     // leader appended offset X (mark, (part<<24)|off)
    ReplFollowerRecv,     // follower applyReplicated ENTRY: push covering X received
    ReplHwAdvance,        // leader HW advanced to cover X (mark, (part<<24)|hw)
    ReplAckSendOff,       // leader enqueued client ack for X (mark, (part<<24)|off)
    ReplOutEnqueue,       // WORKER enqueues a repl frame to the outbound ring (corr)
    ReplOutDrain,         // CONTROL drains + offers that frame to Aeron (corr) — worker→control handoff
    ReplFragRecv,         // CONTROL enqueues a routed op to the worker ring (corr=recvSeq)
    ReplRouteToWorker,    // (deprecated placeholder — kept for enum stability)
    ReplWorkerOpRun,      // WORKER dequeues+runs the routed op (corr=runSeq); pairs 1:1 with ReplFragRecv (FIFO)

    // --- CLIENT (rune_produce_throughput) — corr = correlationId & 0xFFFFFFFF ---
    ClientDrvSubmit,      // driver: about to enter the window-wait for record i
    ClientDrvSend,        // driver: window cleared → Producer::send() called (gap = window/ack starvation)
    ClientDrvAckCb,       // driver: user ack callback fired for the record
    ClientAppSend,        // app thread: record written to the app→agent ring (corr)
    ClientAgentDrain,     // agent thread: record dequeued from the ring into a batch (corr)
    ClientBatchOffer,     // agent thread: batch frame offered to the broker (corr=batch)
    ClientAgentDoWork,    // agent thread: one duty cycle (inter-tick gap = agent park)
    ClientAckComplete,    // agent thread: ProduceResponse matched → callback/promise fired (corr)

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

    // --- per-worker replication transport (Category::Replication) — #4 deep instrumentation ---
    ReplWorkerFetchReqSend,   // follower: WorkerReplicationTransport::sendFetchRequest (broadcast)
    ReplWorkerSend,           // leader: sendFetchResponse — build + encode into scratch
    ReplWorkerSendOnPub,      // leader: sendOnPub_ — tryClaim/offer to a peer's Aeron pub
    ReplWorkerBroadcast,      // broadcastToPeers loop (offer to every peer pub)
    ReplWorkerPoll,           // WorkerReplicationTransport::poll — drain inbound sub (per doWork)
    ReplWorkerDispatch,       // dispatchFrame_ — decode header + route + inline apply (per frame)

    // --- worker doWork sub-stages (Category::Runtime) — where a doWork tick spends time ---
    WorkerDrainIngress,       // drainIngress_ (produce ingress drain)
    WorkerReplicaDoWorkStage, // owned-replica doWork loop (all owned replicas)
    WorkerReplPollDrive,      // doWork's call into the per-worker replication poll
    WorkerEgressPushStage,    // pushOwnedSubscriptions_ (consumer egress)

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
    {"ReplLeaderAppend", Category::Replication},
    {"ReplFollowerRecv", Category::Replication},
    {"ReplHwAdvance", Category::Replication},
    {"ReplAckSendOff", Category::Replication},
    {"ReplOutEnqueue", Category::Replication},
    {"ReplOutDrain", Category::Replication},
    {"ReplFragRecv", Category::Replication},
    {"ReplRouteToWorker", Category::Replication},
    {"ReplWorkerOpRun", Category::Replication},
    {"ClientDrvSubmit", Category::Produce},
    {"ClientDrvSend", Category::Produce},
    {"ClientDrvAckCb", Category::Produce},
    {"ClientAppSend", Category::Produce},
    {"ClientAgentDrain", Category::Produce},
    {"ClientBatchOffer", Category::Produce},
    {"ClientAgentDoWork", Category::Produce},
    {"ClientAckComplete", Category::Produce},

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

    // per-worker replication transport (#4 deep instrumentation)
    {"ReplWorkerFetchReqSend", Category::Replication},
    {"ReplWorkerSend", Category::Replication},
    {"ReplWorkerSendOnPub", Category::Replication},
    {"ReplWorkerBroadcast", Category::Replication},
    {"ReplWorkerPoll", Category::Replication},
    {"ReplWorkerDispatch", Category::Replication},

    // worker doWork sub-stages
    {"WorkerDrainIngress", Category::Runtime},
    {"WorkerReplicaDoWorkStage", Category::Runtime},
    {"WorkerReplPollDrive", Category::Runtime},
    {"WorkerEgressPushStage", Category::Runtime},
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
