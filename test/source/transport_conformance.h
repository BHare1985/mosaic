#pragma once

#include <functional>
#include <memory>
#include <mosaic/net/transport.h>

namespace mosaic::test {

/// Two connected endpoints plus a way to let delivery happen (advance simulated time, or wait for a
/// socket). This is what keeps the suite transport-agnostic: it never touches a concrete transport.
struct TransportPair {
    /// Declared first so it is destroyed last: the transports reference whatever lives in here.
    std::shared_ptr<void> owner;
    std::unique_ptr<Transport> a;
    std::unique_ptr<Transport> b;
    std::function<void()> settle;
};

using TransportFactory = std::function<TransportPair()>;

/// The transport contract as a reusable artefact (`openspec/specs/net/transport/spec.md`): best-effort
/// delivery, no self-delivery, publish filtered by subscription, silent drop for unknown peers, stable
/// identity, non-blocking poll. A new transport is verified by supplying a factory and nothing else
/// (NET-0).
void run_transport_conformance(const TransportFactory& factory);

} // namespace mosaic::test
