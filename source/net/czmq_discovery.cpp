#include <mosaic/net/czmq_discovery.h>

#include <cstdio>
#include <czmq.h>
#include <memory>
#include <sstream>

using std::getline;
using std::istringstream;
using std::move;
using std::ostringstream;
using std::string;
using std::vector;

namespace mosaic {
namespace {

/// Beacon payload: the cluster first (so zbeacon's prefix filter scopes it), then a version, then
/// the peer. One line, well under UDP_FRAME_MAX.
string encode_announcement(const string& cluster, const DiscoveredPeer& self) {
    ostringstream out;
    out << cluster << "|1|" << self.id << '|' << self.endpoint << '|' << self.curve_public;
    return out.str();
}

bool decode_announcement(const string& cluster, const string& text, DiscoveredPeer& out) {
    vector<string> fields;
    istringstream stream(text);
    string field;
    while (getline(stream, field, '|'))
        fields.push_back(field);
    if (fields.size() < 4 || fields[0] != cluster || fields[1] != "1")
        return false;
    out.id = fields[2];
    out.endpoint = fields[3];
    out.curve_public = fields.size() > 4 ? fields[4] : string{};
    return !out.id.empty() && !out.endpoint.empty();
}

}  // namespace

struct CzmqDiscovery::Impl {
    CzmqDiscoveryConfig config;
    zactor_t* beacon = nullptr;
};

CzmqDiscovery::CzmqDiscovery(CzmqDiscoveryConfig config) : impl_(*new Impl()) {
    impl_.config = move(config);
    if (!impl_.config.interface.empty())
        zsys_set_interface(impl_.config.interface.c_str());

    // ---- beacon: who is on this network ----
    if (impl_.config.beacon_port != 0) {
        impl_.beacon = zactor_new(zbeacon, nullptr);
        if (impl_.beacon != nullptr) {
            const auto port = std::to_string(impl_.config.beacon_port);
            zstr_sendx(impl_.beacon, "CONFIGURE", port.c_str(), nullptr);
            char* hostname =
                zstr_recv(impl_.beacon);  // the actor answers with the host it configured
            const bool configured = hostname != nullptr && *hostname != '\0';
            zstr_free(&hostname);
            if (configured) {
                const auto announcement =
                    encode_announcement(impl_.config.cluster, impl_.config.self);
                // The cluster is the beacon's *filter*, not a command of its own: zbeacon asserts
                // on an unknown command, which aborts the whole process (found the hard way).
                zstr_sendx(impl_.beacon, "SUBSCRIBE", impl_.config.cluster.c_str(), nullptr);
                zsock_send(zactor_sock(impl_.beacon), "sbi", "PUBLISH", announcement.data(),
                           announcement.size(), static_cast<int>(impl_.config.beacon_interval_ms));
            } else {
                // No broadcast-capable interface: report it rather than pretending the LAN is
                // quiet.
                zactor_destroy(&impl_.beacon);
            }
        }
    }
}

CzmqDiscovery::~CzmqDiscovery() {
    if (impl_.beacon != nullptr)
        zactor_destroy(&impl_.beacon);
    delete &impl_;
}

const char* CzmqDiscovery::kind() const {
    return impl_.beacon != nullptr ? "beacon" : "none";
}

bool CzmqDiscovery::beacon_active() const {
    return impl_.beacon != nullptr;
}

vector<DiscoveredPeer> CzmqDiscovery::poll_discovered() {
    vector<DiscoveredPeer> found;

    // Beacon: the actor delivers [address][data]; draining is non-blocking so the node loop stays
    // in charge.
    while (impl_.beacon != nullptr) {
        zmsg_t* message = zmsg_recv_nowait(zactor_sock(impl_.beacon));
        if (message == nullptr)
            break;
        zframe_t* address = zmsg_pop(message);
        zframe_t* data = zmsg_pop(message);
        if (data != nullptr) {
            const auto size = zframe_size(data);
            const string text(reinterpret_cast<const char*>(zframe_data(data)), size);
            DiscoveredPeer peer;
            if (decode_announcement(impl_.config.cluster, text, peer) &&
                peer.id != impl_.config.self.id)
                found.push_back(peer);
        }
        zframe_destroy(&address);
        zframe_destroy(&data);
        zmsg_destroy(&message);
    }

    return found;
}

}  // namespace mosaic
