#include <mosaic/log/rotation.h>

#include <algorithm>

using std::map;
using std::pair;
using std::uint32_t;
using std::vector;

namespace mosaic {
namespace {
/// The last rotation link in a log wins: a writer cannot be told two different successors by the
/// same history, and if it did, the later statement is the one it made last.
std::optional<RotationLink> latest_link(const RotationChain& chain, const LogStore& store,
                                        const Verifier& verifier, const WriterId& writer,
                                        const Channel& channel) {
    (void)chain;
    std::optional<RotationLink> found;
    const LogKey key{.writer = writer, .channel = channel};
    const auto head = store.head_seq(key);
    if (!head) {
        return std::nullopt;
    }
    // Scan the log from the first record: links are rare, and a store read per record is the price
    // of not trusting a cache that a restart would empty anyway.
    const auto records = store.read(key, 0, static_cast<std::size_t>(*head) + 1);
    for (const auto& record : records) {
        if (record.content_type != kRotationContentType) {
            continue;
        }
        // The signature was checked at ingest, and is checked again here because resolution can run
        // before a store has been replayed.
        const auto hash = record_hash_tagged(record);
        if (!hash || !verifier.verify(record.writer, *hash, record.sig)) {
            continue;
        }
        if (const auto link = decode_rotation_link(record.payload)) {
            found = link;
        }
    }
    return found;
}
}  // namespace

RotationChain::RotationChain(const LogStore& store, const Verifier& verifier, uint32_t max_depth)
    : store_(store), verifier_(verifier), max_depth_(max_depth) {}

RotationChain::Resolution RotationChain::walk(const WriterId& root, const Channel& channel) const {
    Resolution resolution;
    resolution.identities.push_back(root);

    WriterId current = root;
    for (uint32_t depth = 0; depth < max_depth_; ++depth) {
        const auto link = latest_link(*this, store_, verifier_, current, channel);
        if (!link) {
            return resolution;  // the chain ends here, which is the normal case
        }
        if (link->successor.empty()) {
            resolution.refused = true;
            resolution.reason = "link names no successor";
            return resolution;
        }
        // A cycle, or the same identity twice, makes two logs the same log - exactly the ambiguity
        // fork evidence exists to prevent.
        if (std::ranges::find(resolution.identities, link->successor) !=
            resolution.identities.end()) {
            resolution.refused = true;
            resolution.reason = "chain repeats an identity";
            return resolution;
        }
        resolution.identities.push_back(link->successor);
        current = link->successor;
    }

    // The walk used its whole depth budget and the last identity still had a link: fail closed
    // rather than treating "we stopped looking" as "there is nothing more".
    if (latest_link(*this, store_, verifier_, current, channel)) {
        resolution.refused = true;
        resolution.reason = "chain longer than the configured depth";
    }
    return resolution;
}

const RotationChain::Resolution& RotationChain::resolve(const WriterId& root,
                                                        const Channel& channel) {
    const auto key = pair<WriterId, Channel>{root, channel};
    const auto cached = cache_.find(key);
    if (cached != cache_.end()) {
        return cached->second;
    }
    return cache_.emplace(key, walk(root, channel)).first->second;
}

void RotationChain::invalidate(const WriterId& root, const Channel& channel) {
    cache_.erase({root, channel});
}

void RotationChain::invalidate_all() {
    cache_.clear();
}

}  // namespace mosaic
