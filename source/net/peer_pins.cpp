#include <mosaic/net/peer_pins.h>

#include <fstream>
#include <sstream>

namespace mosaic {
namespace {
    constexpr const char* kHeader = "mosaic-pins 1";
} // namespace

const char* to_string(PinVerdict verdict)
{
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

std::optional<PeerPins> PeerPins::load(const std::string& path, Mode mode)
{
    PeerPins pins(mode);
    std::ifstream in(path);
    if (!in)
        return pins; // first run: an empty store, not a failure

    std::string header;
    if (!std::getline(in, header) || header != kHeader)
        return std::nullopt; // a file we do not understand must not silently accept anyone

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        std::istringstream stream(line);
        std::string peer;
        std::string key;
        if (!(stream >> peer >> key))
            return std::nullopt;
        pins.pins_[peer] = key;
    }
    return pins;
}

bool PeerPins::save(const std::string& path) const
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return false;
    out << kHeader << "\n";
    for (const auto& [peer, key] : pins_)
        out << peer << " " << key << "\n";
    return out.good();
}

void PeerPins::pin(const std::string& peer_id, const std::string& key_z85) { pins_[peer_id] = key_z85; }

PinVerdict PeerPins::observe(const std::string& peer_id, const std::string& key_z85)
{
    const auto it = pins_.find(peer_id);
    if (it == pins_.end()) {
        if (mode_ == Mode::Closed)
            return PinVerdict::NotListed;
        pins_[peer_id] = key_z85;
        return PinVerdict::AllowedAndPinned;
    }
    return it->second == key_z85 ? PinVerdict::Allowed : PinVerdict::Mismatch;
}

bool PeerPins::allows_key(const std::string& key_z85) const
{
    for (const auto& [peer, key] : pins_)
        if (key == key_z85)
            return true;
    return false;
}

PinVerdict PeerPins::decide(const std::string& who, const std::string& key_z85)
{
    if (mode_ == Mode::Closed)
        return allows_key(key_z85) ? PinVerdict::Allowed : PinVerdict::NotListed;
    return observe(who, key_z85);
}

std::optional<std::string> PeerPins::pinned(const std::string& peer_id) const
{
    const auto it = pins_.find(peer_id);
    if (it == pins_.end())
        return std::nullopt;
    return it->second;
}

} // namespace mosaic
