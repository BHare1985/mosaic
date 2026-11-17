#include <mosaic/net/peer_pins.h>

#include <fstream>
#include <sstream>

using std::getline;
using std::ifstream;
using std::istringstream;
using std::nullopt;
using std::ofstream;
using std::optional;
using std::string;

namespace mosaic {
namespace {
constexpr const char* kHeader = "mosaic-pins 1";
}  // namespace

const char* to_string(PinVerdict verdict) {
    switch (verdict) {
        case PinVerdict::Allowed:
            return "allowed";
        case PinVerdict::AllowedAndPinned:
            return "allowed-and-pinned";
        case PinVerdict::Mismatch:
            return "mismatch";
        case PinVerdict::NotListed:
            return "not-listed";
    }
    return "?";
}

optional<PeerPins> PeerPins::load(const string& path, Mode mode) {
    PeerPins pins(mode);
    ifstream in(path);
    if (!in)
        return pins;  // first run: an empty store, not a failure

    string header;
    if (!getline(in, header) || header != kHeader)
        return nullopt;  // a file we do not understand must not silently accept anyone

    string line;
    while (getline(in, line)) {
        if (line.empty())
            continue;
        istringstream stream(line);
        string peer;
        string key;
        if (!(stream >> peer >> key))
            return nullopt;
        pins.pins_[peer] = key;
    }
    return pins;
}

bool PeerPins::save(const string& path) const {
    ofstream out(path, std::ios::trunc);
    if (!out)
        return false;
    out << kHeader << "\n";
    for (const auto& [peer, key] : pins_)
        out << peer << " " << key << "\n";
    return out.good();
}

void PeerPins::pin(const string& peer_id, const string& key_z85) {
    pins_[peer_id] = key_z85;
}

PinVerdict PeerPins::observe(const string& peer_id, const string& key_z85) {
    const auto it = pins_.find(peer_id);
    if (it == pins_.end()) {
        if (mode_ == Mode::Closed)
            return PinVerdict::NotListed;
        pins_[peer_id] = key_z85;
        return PinVerdict::AllowedAndPinned;
    }
    return it->second == key_z85 ? PinVerdict::Allowed : PinVerdict::Mismatch;
}

bool PeerPins::allows_key(const string& key_z85) const {
    for (const auto& [peer, key] : pins_)
        if (key == key_z85)
            return true;
    return false;
}

PinVerdict PeerPins::decide(const string& who, const string& key_z85) {
    if (mode_ == Mode::Closed)
        return allows_key(key_z85) ? PinVerdict::Allowed : PinVerdict::NotListed;
    return observe(who, key_z85);
}

optional<string> PeerPins::pinned(const string& peer_id) const {
    const auto it = pins_.find(peer_id);
    if (it == pins_.end())
        return nullopt;
    return it->second;
}

}  // namespace mosaic
