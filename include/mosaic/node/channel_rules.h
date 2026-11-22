#pragma once

// Which writer identities may write which channels (security/channel-authorization). Open by
// default: a channel with no rule accepts any verified writer, so nothing changes unless a
// deployment asks for a closed channel - and that is what makes the job layer safe to expose, since
// `svc.<name>` is just a channel that any key holder could otherwise publish to.
//
// Rules match with the subscription language, so there is one pattern dialect in the system rather
// than two.

#include <mosaic/core/types.h>
#include <mosaic/log/rotation.h>
#include <mosaic/match/matcher.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mosaic {

class ChannelRules {
public:
    /// One entry: a pattern, the identities allowed on it, and whether those identities are
    /// rotation *roots* rather than literal writers. A root authorizes itself and its bounded
    /// successors, so a writer that rotates its key does not need every peer to edit its
    /// configuration at the same moment (SEC-3).
    struct Rule {
        std::string pattern;
        std::vector<WriterId> writers;
        bool rotation_root = false;
    };

    /// Permit only `writers` to write channels matching `pattern`. False for an invalid pattern.
    bool allow(const std::string& pattern, std::vector<WriterId> writers);

    /// Permit `roots` and whatever they legitimately rotate into. The chain is consulted per
    /// decision, from each root, and a chain that is refused authorizes only its root.
    bool allow_rotation(const std::string& pattern, std::vector<WriterId> roots);

    /// May `writer` write `channel`? True when no rule matches: open is the default, deliberately.
    /// Literal entries only - a rotation root authorizes nobody here, because this overload has no
    /// chain to consult.
    bool permits(const Channel& channel, const WriterId& writer) const;

    /// The same decision, consulting `chain` for rotation roots. A successor is authorized when it
    /// is reachable from a named root; a predecessor is not, so retiring a key by rotating away
    /// from it retires something.
    bool permits(const Channel& channel, const WriterId& writer, RotationChain& chain) const;

    /// The rule that decided a channel, or empty when the channel is open. Used for counting and
    /// echoing.
    std::optional<std::string> rule_for(const Channel& channel) const;

    /// Every rule, in the order added, for an operator to see what is in force at startup.
    const std::vector<Rule>& rules() const {
        return rules_;
    }
    std::size_t size() const {
        return rules_.size();
    }

private:
    Matcher matcher_;  // one subscription per rule; the SubId is the rule's index
    std::vector<Rule> rules_;
};

}  // namespace mosaic
