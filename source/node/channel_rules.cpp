#include <mosaic/node/channel_rules.h>

#include <algorithm>

using std::find;
using std::move;
using std::nullopt;
using std::optional;
using std::string;
using std::vector;

namespace mosaic {

bool ChannelRules::allow(const string& pattern, vector<WriterId> writers) {
    // The matcher assigns a dense id in add order, which is exactly the rule index we need.
    const auto id = matcher_.add(pattern);
    if (!id) {
        return false;
    }
    if (rules_.size() <= *id) {
        rules_.resize(*id + 1, Rule{.pattern = pattern, .writers = {}});
    }
    rules_[*id] = Rule{.pattern = pattern, .writers = std::move(writers), .rotation_root = false};
    return true;
}

bool ChannelRules::allow_rotation(const string& pattern, vector<WriterId> roots) {
    const auto id = matcher_.add(pattern);
    if (!id) {
        return false;
    }
    if (rules_.size() <= *id) {
        rules_.resize(*id + 1, Rule{.pattern = pattern, .writers = {}});
    }
    rules_[*id] = Rule{.pattern = pattern, .writers = std::move(roots), .rotation_root = true};
    return true;
}

bool ChannelRules::permits(const Channel& channel, const WriterId& writer) const {
    // A channel may match several rules; each one is a statement about who may write there, so the
    // first matching rule that does not name the writer refuses. Anything unmatched is open.
    for (const auto id : matcher_.match(channel)) {
        if (id >= rules_.size()) {
            continue;
        }
        const auto& permitted = rules_[id].writers;
        return std::ranges::find(permitted, writer) != permitted.end();
    }
    return true;
}

bool ChannelRules::permits(const Channel& channel, const WriterId& writer,
                           RotationChain& chain) const {
    for (const auto id : matcher_.match(channel)) {
        if (id >= rules_.size()) {
            continue;
        }
        const auto& rule = rules_[id];
        if (std::ranges::find(rule.writers, writer) != rule.writers.end()) {
            return true;
        }
        if (!rule.rotation_root) {
            return false;
        }
        // A root authorizes itself and its bounded successors. A chain that is refused authorizes
        // only the root, which the check above already made: "not trusted" never becomes "trusted
        // by default".
        for (const auto& root : rule.writers) {
            const auto& resolved = chain.resolve(root, channel);
            if (resolved.refused) {
                continue;
            }
            if (std::ranges::find(resolved.identities, writer) != resolved.identities.end()) {
                return true;
            }
        }
        return false;
    }
    return true;
}

optional<string> ChannelRules::rule_for(const Channel& channel) const {
    const auto hits = matcher_.match(channel);
    if (hits.empty() || hits.front() >= rules_.size()) {
        return nullopt;
    }
    return rules_[hits.front()].pattern;
}

}  // namespace mosaic
