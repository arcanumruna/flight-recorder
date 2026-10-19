# Sites and categories

A **site** is a statically registered measurement point: a stable 16-bit id (`flight_recorder::Site`) with a name and a
**category** (`flight_recorder::Category`). The hot path carries only the id; names are resolved at report time
(`nameOf(Site)`, `categoryOf(Site)`). This table is generated from `include/flight_recorder/Sites.h`, which is the single
source of truth: `kSiteMeta` must have one entry per enumerator, enforced by a `static_assert`.

There are **146** sites in 7 categories: produce (34), replication (75), read (3), consensus (3), storage (8), egress (3), runtime (20).

The category selects both the compile-time switch (`-DFLIGHT_RECORDER_NO_<CATEGORY>`) and the runtime mask
(`FLIGHT_RECORDER_CATEGORIES`, `setCategoryMask`); see [configuration](configuration.md). Several sites are *marks*
(an instant, start equal to end) whose `corr` is a correlation key built with `partKey` or `foldCorr` (see [the C++ API](api/cpp.md#correlation-keys)).

## Produce

| Site | Meaning |
|---|---|
| `IngressRecv` | gateway ingress subscription delivered a client frame |
| `GatewayHandle` | gateway handleProduce entry→exit |
| `WorkerEnqueue` | gateway → owning worker tryEnqueueOp (produce op) |
| `WorkerAppend` | worker applies produce op → append to log |
| `ClientAckRecv` | (client-side) ack received — Category::Produce |
| `GatewayProduceDecode` | decode the produce frame / record batch |
| `GatewayRouteProduce` | routing-image lookup + leader/redirect decision |
| `WorkerProcessOp` | worker dequeues + dispatches one WorkerOp |
| `ProduceAckEncode` | encode ProduceResponse |
| `ProduceAckSend` | sendRespReliable / enqueueOrBestEffort of the ack |
| `WorkerIngressRecv` | produce frame received + handled at the worker ingress |
| `ClientDrvSubmit` | driver: about to enter the window-wait for record i |
| `ClientDrvSend` | driver: window cleared → Producer::send() called (gap = window/ack starvation) |
| `ClientDrvAckCb` | driver: user ack callback fired for the record |
| `ClientAppSend` | app thread: record written to the app→agent ring (corr) |
| `ClientAgentDrain` | agent thread: record dequeued from the ring into a batch (corr) |
| `ClientBatchOffer` | agent thread: batch frame offered to the broker (corr=batch) |
| `ClientAgentDoWork` | agent thread: one duty cycle (inter-tick gap = agent park) |
| `ClientAckComplete` | agent thread: ProduceResponse matched → callback/promise fired (corr) |
| `ShIngressFrame` | Produce: shard direct ingress frame decoded, corr=C, arg=workerId, val=frame bytes |
| `ShAppendCorr` | Produce: frame appended (or answered), corr=C, arg=workerId, val=K(lastOffset) (-errorCode if refused) |
| `ShGatewayRouted` | Produce: gateway-routed produce reached its worker (op ring), corr=C, arg=workerId |
| `ShIntakePaused` | Produce (throttled): frame ABORTed (its session holds acks), corr=(uint32)sessionId |
| `ShAckHold` | Produce: session ack-hold edge, corr=(uint32)sessionId, arg=1 held / 0 released |
| `ClientBatchSeal` | Produce: batch frame built for its first offer, corr=C, arg=records, val=ns since its first record |
| `ClientOfferRefused` | Produce (throttled): batch offer refused (kept, retried), corr=C, arg=partition |
| `ClientTransportRefused` | Produce (throttled): client Aeron offer/claim refused, corr=streamId, arg=-position |
| `ClientRetransmit` | Produce: stored frame re-offered, corr=C, arg=reason (1 timer, 2 route, 3 parked, 4 redirect), val=attempts |
| `ClientNotLeader` | Produce: NotLeader answer, partition route-parked, corr=C, arg=partition |
| `ClientOosPark` | Produce: OutOfOrderSequence park, corr=C, arg=1 older-live / 2 resend / 3 lane-wait, val=parked |
| `ClientLaneWait` | Produce: lane wait entered, corr=C, arg=partition |
| `ClientWindowFull` | Produce (throttled): batch due but the in-flight window is full, corr=partition |
| `ClientRetriable` | Produce: retriable error answer (backoff), corr=C, arg=errorCode |
| `GatewayOfferRefused` | Produce (throttled): gateway egress offer refused, corr=streamId, arg=-position (0 = not connected) |

## Replication

| Site | Meaning |
|---|---|
| `ReplIngressPoll` | AeronPartitionTransport::poll (controlledPoll) |
| `ReplOnFragment` | onFragment dispatch of one inbound frame |
| `ReplFetchReqHandle` | leader onFetchRequest (read log + build response) |
| `ReplFetchRespApply` | follower applyReplicated |
| `ReplSendFetchResp` | sendFetchResponse encode + enqueueOutbound |
| `ReplOutboundDrain` | drainOutbound (control thread offers to peers) |
| `ReplEnqueueOutbound` | enqueueOutbound (worker → outbound ring) |
| `ReplPushToFollowers` | leader pushToFollowers after append (GR-10) |
| `ReplPendingAckEnqueue` | acks=all: produce parked in pendingAcksAll_ (mark, corr) |
| `ReplPendingAckFire` | acks=all: HW reached → ack fired (mark, corr) |
| `ReplLeaderAppend` | leader appended offset X (mark, (part<<24)\|off) |
| `ReplFollowerRecv` | follower applyReplicated ENTRY: push covering X received |
| `ReplHwAdvance` | leader HW advanced to cover X (mark, (part<<24)\|hw) |
| `ReplAckSendOff` | leader enqueued client ack for X (mark, (part<<24)\|off) |
| `ReplOutEnqueue` | WORKER enqueues a repl frame to the outbound ring (corr) |
| `ReplOutDrain` | CONTROL drains + offers that frame to Aeron (corr) — worker→control handoff |
| `ReplFragRecv` | CONTROL enqueues a routed op to the worker ring (corr=recvSeq) |
| `ReplRouteToWorker` | (deprecated placeholder — kept for enum stability) |
| `ReplWorkerOpRun` | WORKER dequeues+runs the routed op (corr=runSeq); pairs 1:1 with ReplFragRecv (FIFO) |
| `ReplWorkerFetchReqSend` | follower: WorkerReplicationTransport::sendFetchRequest (broadcast) |
| `ReplWorkerSend` | leader: sendFetchResponse — build + encode into scratch |
| `ReplWorkerSendOnPub` | leader: sendOnPub_ — tryClaim/offer to a peer's Aeron pub |
| `ReplWorkerBroadcast` | broadcastToPeers loop (offer to every peer pub) |
| `ReplWorkerPoll` | WorkerReplicationTransport::poll — drain inbound sub (per doWork) |
| `ReplWorkerDispatch` | dispatchFrame_ — decode header + route + inline apply (per frame) |
| `ShAppendBase` | leader: batch appended, corr=K(baseOffset), val=lastOffset |
| `ShPushAttempt` | leader: push to one follower attempted, corr=K(endOffset-1), val=follower |
| `ShPushSent` | leader: push accepted by transport, corr=K(endOffset-1), val=follower |
| `ShPushRefused` | leader (throttled): push refused, corr=K(fromOffset), arg=follower |
| `ShPushWindowBlocked` | leader (throttled): push skipped at the flow window, corr=K(pushFloor) |
| `ShFetchRecv` | leader: fetch/ack received, corr=K(fetchOffset), val=(follower<<1)\|wantData |
| `ShServeDecision` | leader: serve decision, corr=K(fetchOffset), val=(follower<<2)\|serve |
| `ShAnswerSent` | leader: fetch answer accepted, corr=K(fetchOffset), val=record bytes |
| `ShAnswerRefused` | leader (throttled): fetch answer refused, corr=K(fetchOffset), arg=follower |
| `ShHwAdvanceFetch` | leader: HW advanced, corr=K(hw-1), val=follower (-1 = leader recompute) |
| `ShWaiterPark` | leader worker: acks=all waiter parked, corr=C, val=K(lastOffset) |
| `ShWaiterFire` | leader worker: waiter fired, corr=C, val=errorCode |
| `ShAckClaimed` | leader worker: ProduceResponse claimed on the session pub, corr=C, val=baseOffset |
| `ShAckClaimBlocked` | leader worker (throttled): ack claim refused, corr=C, arg=-claim position |
| `ShApplyEntry` | follower: applyReplicated entry, corr=K(leoBefore), val=record bytes |
| `ShApplyDone` | follower: applied, corr=K(leoAfter-1), val=leoBefore |
| `ShFastAckSent` | follower: fast-ack sent, corr=K(leo), val=hw |
| `ShFastAckRefused` | follower (throttled): fast-ack refused, corr=K(leo) |
| `ShFetchSent` | follower: fetch emitted, corr=K(fetchOffset), val=wantData |
| `ShFetchRefused` | follower (throttled): fetch refused, corr=K(fetchOffset), arg=wantData |
| `ShPlanePollFrags` | worker: replication plane poll handled fragments, corr=workerId, val=fragments |
| `ShPlanePubRefused` | worker (throttled): plane pub offer/claim refused, corr=peer, arg=-position |
| `ShDriverWork` | shard (throttled): driver still busy after the per-pass duty cap, corr=workerId |
| `ShPassIdle` | (reserved) |
| `ShIngressFrags` | worker: client ingress poll handled frames, corr=workerId, val=frames |
| `AeronConductorDuty` | aeron conductor do_work (span) |
| `AeronSenderDuty` | aeron sender do_work (span) |
| `AeronReceiverDuty` | aeron receiver do_work (span) |
| `AeronSendData` | aeron data frame sent (mark corr=(stream&0xFF)<<24\|termOffset, value=bytes) |
| `AeronRecvInsert` | aeron data inserted into image (mark, same corr, value=len) |
| `AeronSmSent` | aeron status message sent (mark corr=stream) |
| `AeronSmRecv` | aeron status message received at publication (mark corr=stream) |
| `AeronSendSyscall` | aeron sendmmsg/sendmsg syscall (span, corr=messages) |
| `AeronRecvSyscall` |  |
| `AeronSenderDoSend` | sender do_send over all publications (span, value=bytes) |
| `AeronSenderControlPoll` | (reserved) |
| `AeronPubSend` | one publication's send() (span, corr=stream, value=bytes) |
| `AeronSendBlocked` | (reserved) |
| `AeronHeartbeatSent` | heartbeat sent (mark corr=stream) |
| `AeronSetupSent` | (reserved) |
| `AeronRetransmit` | retransmit/resend (mark corr=stream, value=len) |
| `AeronNakRecv` | NAK received by publication (mark corr=stream, value=len) |
| `AeronNakSent` | NAK sent by image (mark corr=stream) |
| `AeronLossGap` | loss gap detected (mark corr=stream, value=len) |
| `AeronReceiverPoll` | receiver transport poller poll (span, value=datagrams) |
| `AeronImageClean` | image track_rebuild incl. term cleaning (span, corr=stream) |
| `AeronPubClean` | publication update_pub_lmt incl. cleaning (span, corr=stream) |
| `AeronConductorCmd` | (reserved) |
| `AeronRecvDispatch` | data packet dispatch (span, corr=(stream&0xFF)<<24\|termOffset, value=len) |
| `ReplCtlOfferRefused` | Replication (throttled): control-thread replication offer refused, corr=streamId, arg=-position |

## Read

| Site | Meaning |
|---|---|
| `ReadSubscribe` | subscribe/unsubscribe handling |
| `ReadLogRead` | zero-copy Log::readView |
| `ReadEncodePush` | encode records + push to consumer egress |

## Consensus

| Site | Meaning |
|---|---|
| `ConsensusDoWork` | NodeConsensusAgent duty cycle |
| `ConsensusIngressPoll` | aeron ingress poll → IngressRing fill |
| `ElectionStep` | Election::doWork |

## Storage

| Site | Meaning |
|---|---|
| `StorageWrite` | storage append (pwritev) — Category::Storage |
| `StorageFlush` | fsync / flush / durability barrier |
| `StoragePwrite` | pwrite/pwritev syscall only (corr=bytes) |
| `StorageRoll` | Log::maybeRoll only |
| `StorageIndex` | offset-index append only |
| `StorageFsyncSubmit` | Storage: io flush submitted, corr=token, arg=kind (1 durable, 2 background, 3 seal) |
| `StorageFsyncDone` | Storage: io flush completion reaped, corr=token, val=errorCode |
| `StorageFsync` | Storage: fdatasync syscall on the portable flusher thread (span), corr=fd |

## Egress

| Site | Meaning |
|---|---|
| `EgressClaim` | egress publication tryClaim — Category::Egress |
| `EgressSend` | egress publication commit/offer — Category::Egress |
| `EgressPush` | worker read→encode→push to a consumer |

## Runtime

| Site | Meaning |
|---|---|
| `RuntimeDoWork` | NodeRuntimeAgent duty cycle (one tick) |
| `MetadataDoWork` | MetadataService::doWork |
| `WorkerDoWork` | WorkerPool worker duty cycle (one tick) — inter-tick gap |
| `WorkerDrainIngress` | drainIngress_ (produce ingress drain) |
| `WorkerReplicaDoWorkStage` | owned-replica doWork loop (all owned replicas) |
| `WorkerReplPollDrive` | doWork's call into the per-worker replication poll |
| `WorkerEgressPushStage` | pushOwnedSubscriptions_ (consumer egress) |
| `ShardPass` | ShardAgent::doWork — one full shard pass (driver+conductor+worker[+fused runtime]) |
| `ShardDriverDuty` | one invoker-driver duty cycle inside a shard pass (corr=index in pass) |
| `ShIngressSkip` | Runtime (throttled): client-ingress poll skipped, corr=workerId, arg=reason (1 FIFO, 2 ring, 3 budget) |
| `ShOwedCapThrottle` | Runtime (throttled): owed-return cap throttled intake, corr=workerId, arg=1 shard poll / 2 gateway-routed |
| `ShOwedCapEpisode` | Runtime: owed-cap episode edge, corr=workerId, arg=1 enter / 0 leave, val=throttle events |
| `ShRetFifoPause` | Runtime: return-FIFO high-water pause edge, corr=workerId, arg=1 enter / 0 leave, val=backlog |
| `ShStageConductor` | Runtime (slow span): shard pass — own client conductor invoke, corr=workerId |
| `ShStageClientIngress` | Runtime (slow span): shard pass — return FIFO flush + client ingress poll, corr=workerId |
| `ShStageAckFire` | Runtime (slow span): shard pass — acks=all waiter fire, corr=workerId |
| `ShStageDisk` | Runtime (slow span): shard pass — background disk work + io reap, corr=workerId |
| `ShStageColdTail` | Runtime (slow span): shard pass — egress push, cleaner drains, timed sync, signals |
| `ShStageAckFlush` | Runtime (slow span): shard pass — session ack claim flush, corr=workerId |
| `ShStageRuntime` | Runtime (slow span): fused runtime pass (worker 0), corr=workerId |
