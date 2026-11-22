// What a peer pays for misbehaving, and what a ban must never change (spec: security/peer-scoring).
//
// The rule under test throughout: faults earn strikes, strikes decay, crossing a threshold bans,
// and a ban only ever costs the peer its own service - never what the data layer believes.
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/jobs/runtime.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>
#include <variant>

using std::initializer_list;
using std::size_t;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;
using std::vector;

using namespace mosaic;

namespace {
Bytes bytes_of(size_t size, char fill = 'x') {
    return Bytes(size, static_cast<uint8_t>(fill));
}

Bytes text(const string& s) {
    return Bytes(s.begin(), s.end());
}

uint64_t strikes_of(const NodeCounters& counters, PeerFault fault) {
    const auto it = counters.strikes_by_kind.find(fault);
    return it == counters.strikes_by_kind.end() ? 0 : it->second;
}

/// A record signed by `signer`, so a test can put a genuine record on any link it likes. The hash
/// algorithm is stamped from the hasher for the reason in docs/design/conventions.md.
Record signed_record(const Signer& signer, const Hasher& hasher, const Channel& channel, Seq seq,
                     const Hash& prev, const Bytes& payload) {
    Record record;
    record.writer = signer.id();
    record.channel = channel;
    record.hash_algo = hasher.algorithm();
    record.seq = seq;
    record.prev = prev;
    record.timestamp_ms = 0;
    record.content_type = "text/plain";
    record.payload = payload;
    record.sig = signer.sign(record_hash(record, hasher));
    return record;
}

struct Peer {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    vector<string> received;
    vector<BanEvent> bans;
    Peer(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier,
         const NodeConfig& config = {})
        : transport(net, id), node(named(config, id), transport, store, hasher, verifier) {
        node.on_record(
            [this](const Record& r) { received.emplace_back(r.payload.begin(), r.payload.end()); });
        node.on_ban([this](const BanEvent& event) { bans.push_back(event); });
    }
    static NodeConfig named(NodeConfig config, const NodeId& id) {
        config.id = id;
        return config;
    }
};

/// One simulated step: advance the clock (releasing everything due) and let the named nodes work.
void step(SimNetwork& net, uint64_t now_ms, initializer_list<Peer*> peers) {
    net.advance(now_ms);
    for (Peer* peer : peers) {
        peer->node.pump(now_ms);
        peer->node.tick(now_ms);
    }
}

/// Scoring with numbers a test can reach quickly: no decay inside a test's lifetime, a short base
/// ban and a low threshold, unless the case overrides them.
NodeConfig scoring_config() {
    NodeConfig config;
    config.strike_threshold = 2;
    config.strike_decay_ms = 600000;
    config.ban_base_ms = 1000;
    config.ban_max_ms = 4000;
    config.control_interval_ms = 100;
    return config;
}

/// Earn `count` undecodable frames on the `from -> to` link: the one fault a test can inject
/// without hand-writing a wire format.
void fault(SimNetwork& net, const NodeId& from, const NodeId& to, int count) {
    for (int i = 0; i < count; ++i)
        net.inject_undecodable(from, to);
}
}  // namespace

TEST_CASE("fault weights are named once, and ordered by how bad the fault is") {
    // The table docs/design/07-security.md quotes. If a number changes, one of the two fails.
    CHECK(strike_weight(PeerFault::Undecodable) == 1);
    CHECK(strike_weight(PeerFault::Unverified) == 3);
    CHECK(strike_weight(PeerFault::Oversized) == 2);
    CHECK(strike_weight(PeerFault::Unrequested) == 2);
    CHECK(strike_weight(PeerFault::VersionMismatch) == 1);

    // A forgery costs more than a malformed frame; a deployment mistake costs least.
    CHECK(strike_weight(PeerFault::Unverified) > strike_weight(PeerFault::Undecodable));
    CHECK(strike_weight(PeerFault::VersionMismatch) <= strike_weight(PeerFault::Unrequested));

    // Every kind has a stable name, because they are keys in a counter an operator reads.
    CHECK(string(to_string(PeerFault::Undecodable)) == "undecodable");
    CHECK(string(to_string(PeerFault::Unverified)) == "unverified");
    CHECK(string(to_string(PeerFault::Oversized)) == "oversized");
    CHECK(string(to_string(PeerFault::Unrequested)) == "unrequested");
    CHECK(string(to_string(PeerFault::VersionMismatch)) == "version");
}

TEST_CASE("strikes decay in proportion to the time that passed") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(201);

    NodeConfig config;
    config.strike_threshold = 100;  // no ban in this test: only the arithmetic is under test
    config.strike_decay_ms = 1000;
    Peer node(net, "node", hasher, alice, config);

    fault(net, "peer", "node", 6);
    step(net, 0, {&node});
    REQUIRE(node.node.peer_strikes("peer", 0) == 6);

    CHECK(node.node.peer_strikes("peer", 500) == 3);   // half the window, half the strikes
    CHECK(node.node.peer_strikes("peer", 999) == 1);   // proportional, not a step
    CHECK(node.node.peer_strikes("peer", 1000) == 0);  // a full window clears it
    CHECK(node.node.peer_strikes("peer", 5000) == 0);  // and it stays cleared: no history kept

    // The mark only moves when the reduction actually happens, so a caller that steps in tiny
    // increments still decays: otherwise rounding would keep every strike forever.
    fault(net, "peer", "node", 6);
    step(net, 5000, {&node});
    REQUIRE(node.node.peer_strikes("peer", 5000) == 6);
    for (uint64_t now = 5010; now <= 6000; now += 10)
        node.node.pump(now);  // no new faults, just time
    CHECK(node.node.peer_strikes("peer", 6000) == 0);
}

TEST_CASE("each objective fault is counted and earns its strike") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(203);

    NodeConfig config = scoring_config();
    config.strike_threshold = 100;  // count strikes without banning: one kind per assertion
    config.max_record_bytes = 32;
    config.control_interval_ms = 100;
    Peer follower(net, "follower", hasher, alice, config);
    Peer bad(net, "bad", hasher, alice, config);
    follower.node.follow("events.>");

    SUBCASE("an undecodable frame") {
        fault(net, "bad", "follower", 1);
        step(net, 0, {&follower});
        CHECK(strikes_of(follower.node.counters(), PeerFault::Undecodable) == 1);
        CHECK(follower.node.peer_strikes("bad", 0) == 1);
    }

    SUBCASE("a record whose signature does not verify") {
        const auto genuine = signed_record(alice, hasher, "events.a", 0, Hash{}, text("real"));
        Record forged = genuine;
        forged.payload = text("fake");  // the signature still covers the original payload
        bad.transport.send("follower", RecordMsg{forged});
        step(net, 0, {&follower});
        CHECK(follower.node.counters().bad_signature == 1);
        CHECK(strikes_of(follower.node.counters(), PeerFault::Unverified) == 1);
        CHECK(follower.node.peer_strikes("bad", 0) == strike_weight(PeerFault::Unverified));
    }

    SUBCASE("a record whose chain does not verify") {
        // Signed, but the first record of a log cannot commit to a predecessor: a broken chain,
        // not a forgery - and still a fault, because we cannot tell the two apart from the bytes.
        const auto predecessor =
            record_hash(signed_record(alice, hasher, "elsewhere.a", 7, Hash{}, text("x")), hasher);
        Record broken = signed_record(alice, hasher, "events.a", 0, predecessor, text("orphan"));
        bad.transport.send("follower", RecordMsg{broken});
        step(net, 0, {&follower});
        CHECK(follower.node.counters().bad_chain == 1);
        CHECK(strikes_of(follower.node.counters(), PeerFault::Unverified) == 1);
        CHECK(follower.node.peer_strikes("bad", 0) == strike_weight(PeerFault::Unverified));
    }

    SUBCASE("an oversized record") {
        Record big = signed_record(alice, hasher, "events.a", 0, Hash{}, bytes_of(64));
        bad.transport.send("follower", RecordMsg{big});
        step(net, 0, {&follower});
        CHECK(follower.node.counters().oversized_records == 1);
        CHECK(strikes_of(follower.node.counters(), PeerFault::Oversized) == 1);
        CHECK(follower.node.peer_strikes("bad", 0) == strike_weight(PeerFault::Oversized));
    }

    SUBCASE("a repair record nobody asked for") {
        Record volunteered = signed_record(alice, hasher, "events.a", 0, Hash{}, text("unasked"));
        bad.transport.send("follower", DirectRecordMsg{volunteered});
        step(net, 0, {&follower});
        CHECK(strikes_of(follower.node.counters(), PeerFault::Unrequested) == 1);
        CHECK(follower.node.peer_strikes("bad", 0) == strike_weight(PeerFault::Unrequested));
        // A strike never decides whether a record is believed: it verified, so it is stored.
        REQUIRE(follower.node.head({"alice", "events.a"}).has_value());
        CHECK(follower.node.head({"alice", "events.a"})->seq == 0);
    }

    SUBCASE("a hello announcing another protocol version") {
        bad.transport.send("follower", HelloMsg{static_cast<uint8_t>(kProtocolVersion + 5), {}});
        step(net, 0, {&follower});
        CHECK(follower.node.counters().version_rejects == 1);
        CHECK(strikes_of(follower.node.counters(), PeerFault::VersionMismatch) == 1);
        CHECK(follower.node.peer_strikes("bad", 0) == strike_weight(PeerFault::VersionMismatch));
    }
}

TEST_CASE("loss, duplication and lateness earn nothing") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(205);
    net.set_loss(0.3);
    net.set_duplicate(0.1);
    net.set_delay(0, 60);  // jitter: reordering arrives without anything having gone wrong

    NodeConfig config;
    config.strike_threshold = 100;
    config.fetch_batch = 4;
    Peer writer(net, "writer", hasher, alice, config),
        follower(net, "follower", hasher, alice, config);
    follower.node.follow("events.>");

    uint64_t now = 0;
    int published = 0;
    for (int i = 0; i < 4000 && follower.received.size() < 40; ++i) {
        now += 50;
        if (published < 40) {
            writer.node.publish(alice, "events.a", "t",
                                bytes_of(8, static_cast<char>('a' + published % 26)), now);
            ++published;
        }
        step(net, now, {&writer, &follower});
    }
    REQUIRE(follower.received.size() == 40);  // it converged, faults and all

    // The writer lost, duplicated and reordered traffic all the way through. None of that is a
    // fault: a peer that is merely unlucky must never be banned (spec: security/peer-scoring).
    CHECK(follower.node.counters().strikes_by_kind.empty());
    CHECK(follower.node.counters().bans_started == 0);
    CHECK(follower.node.peer_strikes("writer", now) == 0);
    CHECK_FALSE(follower.node.peer_banned("writer", now));
}

TEST_CASE("a fault every window never bans; a burst does") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(207);

    NodeConfig config;
    config.strike_threshold = 8;
    config.strike_decay_ms = 1000;
    config.ban_base_ms = 60000;
    Peer node(net, "node", hasher, alice, config);
    Peer peer(net, "peer", hasher, alice, config);

    // One bad frame per window: it never accumulates, so it never bans.
    uint64_t now = 0;
    uint32_t worst = 0;
    for (int window = 0; window < 20; ++window) {
        fault(net, "peer", "node", 1);
        step(net, now, {&node});
        worst = std::max(worst, node.node.peer_strikes("peer", now));
        now += config.strike_decay_ms;
    }
    CHECK(worst == 1);
    CHECK(node.node.counters().bans_started == 0);
    CHECK_FALSE(node.node.peer_banned("peer", now));

    // A burst in one window does: eight frames now, not spread out.
    fault(net, "peer", "node", 8);
    step(net, now, {&node});
    CHECK(node.node.counters().bans_started == 1);
    CHECK(node.node.peer_banned("peer", now));
    CHECK(node.node.counters().peer_bans == 1);
}

TEST_CASE("a ban doubles with each crossing, up to a ceiling, and lapses on its own") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(209);

    const auto config = scoring_config();  // base 1s, ceiling 4s, threshold 2
    Peer node(net, "node", hasher, alice, config);
    Peer bad(net, "bad", hasher, alice, config);

    uint64_t now = 0;
    const auto ban_now = [&](const NodeId& peer) {
        fault(net, peer, "node", config.strike_threshold);
        step(net, now, {&node});
    };

    ban_now("bad");
    REQUIRE(node.bans.size() == 1);
    CHECK(node.bans.back().edge == BanEdge::Started);
    CHECK(node.bans.back().ban_count == 1);
    CHECK(node.bans.back().duration_ms == 1000);
    CHECK(node.node.counters().bans_started == 1);
    CHECK(node.node.counters().peer_bans == 1);
    CHECK(node.node.peer_banned("bad", now));

    // It lapses with no operator action: the deadline passes and the sweep notices.
    now += 1000;
    step(net, now, {&node});
    CHECK_FALSE(node.node.peer_banned("bad", now));
    CHECK(node.node.counters().peer_bans == 0);
    REQUIRE(node.bans.size() == 2);
    CHECK(node.bans.back().edge == BanEdge::Expired);

    // The second crossing lasts at least twice as long, and the ceiling caps the escalation.
    ban_now("bad");
    REQUIRE(node.bans.size() == 3);
    CHECK(node.bans.back().ban_count == 2);
    CHECK(node.bans.back().duration_ms == 2000);
    now += 2000;
    step(net, now, {&node});
    REQUIRE_FALSE(node.node.peer_banned("bad", now));

    ban_now("bad");
    REQUIRE(node.bans.size() == 5);
    CHECK(node.bans.back().duration_ms == 4000);
    now += 4000;
    step(net, now, {&node});
    REQUIRE_FALSE(node.node.peer_banned("bad", now));

    ban_now("bad");
    CHECK(node.bans.back().duration_ms == config.ban_max_ms);  // clamped, not 8s
    CHECK(node.bans.back().ban_count == 4);
}

TEST_CASE("a ban is lifted early by a hello plus a verifiable record") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(211);

    NodeConfig config;
    config.strike_threshold = 2;
    config.strike_decay_ms = 600000;
    config.ban_base_ms = 600000;  // long enough that only an early lift can end it
    Peer node(net, "node", hasher, alice, config);
    Peer bad(net, "bad", hasher, alice, config);
    node.node.follow("events.>");
    step(net, 0, {&node, &bad});
    step(net, 100, {&node, &bad});  // handshakes settled

    fault(net, "bad", "node", 2);
    step(net, 200, {&node, &bad});
    REQUIRE(node.node.peer_banned("bad", 200));

    // While banned, its traffic is dropped - even a fetch it is allowed to ask for.
    bad.transport.send("node", FetchMsg{{alice.id(), "events.a"}, 0, 4});
    step(net, 300, {&node, &bad});
    CHECK(node.node.counters().banned_drops == 1);

    // A hello speaking our version arms one record's worth of probation, and a record that
    // verifies ends the ban: a ban that only an operator could lift would be a partition.
    const auto genuine = signed_record(alice, hasher, "events.a", 0, Hash{}, text("proof"));
    bad.transport.send("node", HelloMsg{kProtocolVersion, {}});
    bad.transport.send("node", RecordMsg{genuine});
    step(net, 400, {&node, &bad});

    CHECK_FALSE(node.node.peer_banned("bad", 400));
    CHECK(node.node.counters().peer_bans == 0);
    REQUIRE(node.bans.size() == 2);
    CHECK(node.bans.back().edge == BanEdge::Lifted);
    CHECK(node.node.counters().bans_started == 1);   // lifting is not a new ban
    CHECK(node.node.peer_strikes("bad", 400) == 0);  // and the peer starts clean
    REQUIRE(node.node.head({alice.id(), "events.a"}).has_value());
    CHECK(node.received.size() == 1);  // the record that lifted the ban was believed

    // Service resumes with no restart: the next record arrives normally.
    const auto second =
        signed_record(alice, hasher, "events.a", 1, record_hash(genuine, hasher), text("after"));
    bad.transport.send("node", RecordMsg{second});
    step(net, 500, {&node, &bad});
    CHECK(node.received.size() == 2);
}

TEST_CASE("a banned peer is dropped before any work, and gets no repair") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(213);

    const auto config = scoring_config();
    Peer writer(net, "writer", hasher, alice, config);
    Peer replica(net, "replica", hasher, alice, config);
    Peer bad(net, "bad", hasher, alice, config);
    replica.node.follow("events.>");

    for (int i = 0; i < 3; ++i)
        writer.node.publish(alice, "events.a", "t", bytes_of(4, 'a'), 0);
    uint64_t now = 0;
    step(net, now, {&writer, &replica, &bad});
    REQUIRE(replica.node.head({alice.id(), "events.a"})->seq == 2);

    fault(net, "bad", "replica", config.strike_threshold);
    step(net, now, {&replica});
    REQUIRE(replica.node.peer_banned("bad", now));

    // A fetch request from the banned peer is dropped and counted, and nothing is sent back. Only
    // the replica works here, so the reply would still be sitting in `bad`'s inbox.
    bad.transport.send("replica", FetchMsg{{alice.id(), "events.a"}, 0, 10});
    net.advance(now);
    replica.node.pump(now);
    replica.node.tick(now);
    CHECK(replica.node.counters().banned_drops == 1);

    size_t replies = 0;
    while (auto envelope = bad.transport.poll())
        if (std::holds_alternative<DirectRecordMsg>(envelope->message))
            ++replies;
    CHECK(replies == 0);
}

TEST_CASE("a banned follower is not served, and catches up once the ban lapses") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(215);

    NodeConfig config = scoring_config();
    config.ban_base_ms = 600000;  // only the clock can end this ban, so the test controls it
    Peer writer(net, "writer", hasher, alice, config);
    Peer follower(net, "follower", hasher, alice, config);
    follower.node.follow("events.>");

    uint64_t now = 0;
    for (int i = 0; i < 5; ++i)
        writer.node.publish(alice, "events.a", "t", bytes_of(4, 'a' + i), 0);
    step(net, now, {&writer, &follower});
    REQUIRE(follower.received.size() == 5);

    fault(net, "follower", "writer", config.strike_threshold);
    step(net, now, {&writer});
    REQUIRE(writer.node.peer_banned("follower", now));
    CHECK(writer.node.counters().bans_started == 1);

    // It misses live records and has to repair. Repair is exactly what a ban withholds.
    net.partition("writer", "follower");
    for (int i = 0; i < 5; ++i)
        writer.node.publish(alice, "events.a", "t", bytes_of(4, 'z'), now);
    for (int i = 0; i < 40; ++i) {
        now += 25;
        step(net, now, {&writer, &follower});
    }
    CHECK(follower.received.size() == 5);  // five behind
    net.heal("writer", "follower");
    for (int i = 0; i < 80; ++i) {
        now += 25;
        step(net, now, {&writer, &follower});
    }
    CHECK(writer.node.counters().banned_drops > 0);  // it asked, and was refused
    CHECK(follower.received.size() == 5);            // and stayed behind

    // The ban expires by itself and service resumes with no operator action.
    now += config.ban_base_ms;
    step(net, now, {&writer, &follower});
    CHECK_FALSE(writer.node.peer_banned("follower", now));
    for (int i = 0; i < 200 && follower.received.size() < 10; ++i) {
        now += 25;
        step(net, now, {&writer, &follower});
    }
    CHECK(follower.received.size() == 10);
}

TEST_CASE("a misbehaving peer's records stop arriving while a healthy peer's do not") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    InsecureTestSigner mallory("mallory");  // a second writer, so the two logs are distinct
    SimNetwork net(221);

    const auto config = scoring_config();
    Peer anon(net, "anon", hasher, alice, config);  // follows both, writes on its own
    Peer healthy(net, "healthy", hasher, mallory, config);
    Peer follower(net, "follower", hasher, alice, config);
    follower.node.follow("events.>");

    uint64_t now = 0;
    step(net, now, {&anon, &healthy, &follower});
    REQUIRE_FALSE(follower.node.peer_banned("anon", now));

    // The peer that has been sending garbage is banned by the only node that judges it.
    fault(net, "anon", "follower", config.strike_threshold);
    step(net, now, {&follower});
    REQUIRE(follower.node.peer_banned("anon", now));

    // Its records are dropped at the choke point from now on, counted, and nothing reaches the app.
    for (int i = 0; i < 3; ++i)
        anon.node.publish(alice, "events.a", "t", bytes_of(4, 'q'), now);
    step(net, now, {&anon, &healthy, &follower});
    CHECK(follower.received.empty());
    CHECK(follower.node.counters().banned_drops == 3);

    // A healthy peer on the same channel is unaffected: a ban is per peer, never per channel.
    healthy.node.publish(mallory, "events.a", "t", text("good"), now);
    step(net, now, {&anon, &healthy, &follower});
    REQUIRE(follower.received.size() == 1);
    CHECK(follower.received.front() == "good");
    CHECK(follower.node.counters().bans_started == 1);
}

TEST_CASE("a ban cannot change what is believed") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(217);

    const auto config = scoring_config();
    Peer node(net, "node", hasher, alice, config);
    Peer bad(net, "bad", hasher, alice, config);
    Peer other(net, "other", hasher, alice, config);
    node.node.follow("events.>");

    uint64_t now = 0;
    step(net, now, {&node, &bad, &other});

    fault(net, "bad", "node", config.strike_threshold);
    step(net, now, {&node});
    REQUIRE(node.node.peer_banned("bad", now));

    // The banned peer sends a correctly signed record: refused now, because a ban is about service.
    const auto genuine = signed_record(alice, hasher, "events.a", 0, Hash{}, text("true"));
    bad.transport.send("node", RecordMsg{genuine});
    step(net, now, {&node});
    CHECK(node.received.empty());
    REQUIRE(node.node.head({alice.id(), "events.a"}).has_value() == false);

    // The same record from another link is accepted on its signature alone. A ban must never
    // become a way to make a node reject data - that would let an attacker censor a neighbour.
    other.transport.send("node", RecordMsg{genuine});
    step(net, now, {&node});
    REQUIRE(node.node.head({alice.id(), "events.a"}).has_value());
    CHECK(node.node.head({alice.id(), "events.a"})->seq == 0);
    CHECK(node.received.size() == 1);
    CHECK(node.node.counters().bans_started == 1);  // and nobody else was banned for it
}

TEST_CASE("a banned worker is dropped from the job ranking") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(219);

    const auto config = scoring_config();
    Peer w0(net, "w0", hasher, alice, config);
    Peer banned(net, "banned", hasher, alice, config);
    Peer requester(net, "requester", hasher, alice, config);

    // A job id whose top-ranked worker is the one we are about to ban: without the ban w0 would
    // wait a full slot for a worker that will never answer.
    const vector<NodeId> workers{"w0", "banned"};
    string job_id;
    for (int i = 0; i < 1000 && job_id.empty(); ++i) {
        const auto candidate = "job-" + std::to_string(i);
        if (rendezvous_rank(candidate, workers, hasher).front() == "banned")
            job_id = candidate;
    }
    REQUIRE(!job_id.empty());

    int runs = 0;
    JobWorker worker(w0.node, alice, hasher, workers, "resize", [&runs](const JobRequest& job) {
        ++runs;
        JobResult result;
        result.job_id = job.job_id;
        result.ok = true;
        return result;
    });

    fault(net, "banned", "w0", config.strike_threshold);
    net.advance(0);
    w0.node.pump(0);
    w0.node.tick(0);
    REQUIRE(w0.node.peer_banned("banned", 0));

    JobRequest request;
    request.job_id = job_id;
    request.service = "resize";
    request.reply_channel = "res.requester";
    request.timeout_ms = 600;  // two slots of 300ms: a wait here is unmistakable
    requester.node.publish(alice, "svc.resize", kJobRequestContentType, encode_job_request(request),
                           0);

    uint64_t now = 0;
    for (int i = 0; i < 4; ++i) {
        now += 10;
        net.advance(now);
        w0.node.pump(now);
        w0.node.tick(now);
        worker.tick(now);
    }

    // Rank 0 among the peers it still trusts, so it runs at once instead of waiting a slot.
    CHECK(runs == 1);
    CHECK(worker.pending() == 0);
}

TEST_CASE("a fault during probation re-bans for longer, and still counts one banned peer") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(223);

    NodeConfig config;
    config.strike_threshold = 2;
    config.strike_decay_ms = 600000;
    config.ban_base_ms = 1000;
    config.ban_max_ms = 60000;
    Peer node(net, "node", hasher, alice, config);
    Peer bad(net, "bad", hasher, alice, config);
    node.node.follow("events.>");

    uint64_t now = 0;
    step(net, now, {&node, &bad});
    fault(net, "bad", "node", 2);
    step(net, now, {&node, &bad});
    REQUIRE(node.node.peer_banned("bad", now));
    REQUIRE(node.node.counters().peer_bans == 1);
    REQUIRE(node.bans.size() == 1);
    CHECK(node.bans.back().duration_ms == 1000);

    // The peer asks for a fresh start with a hello, then wastes the one record it was given.
    Record forged = signed_record(alice, hasher, "events.a", 0, Hash{}, text("real"));
    forged.payload = text("fake");
    bad.transport.send("node", HelloMsg{kProtocolVersion, {}});
    bad.transport.send("node", RecordMsg{forged});
    step(net, now, {&node, &bad});

    REQUIRE(node.bans.size() == 2);
    CHECK(node.bans.back().edge == BanEdge::Started);
    CHECK(node.bans.back().duration_ms == 2000);                          // doubled
    CHECK(node.node.counters().peer_bans == 1);                           // still one peer, not two
    CHECK(node.node.counters().bans_started == 2);                        // two edges
    CHECK(strikes_of(node.node.counters(), PeerFault::Unverified) == 1);  // the fault was seen
    CHECK(strikes_of(node.node.counters(), PeerFault::Undecodable) == 2);  // and nothing vanished

    // Expiry brings the gauge back to zero: a counter that only ever grew would be a slow lie.
    now += 2000;
    step(net, now, {&node, &bad});
    CHECK(node.node.counters().peer_bans == 0);
    CHECK_FALSE(node.node.peer_banned("bad", now));
}
