#pragma once

// Which writer identities may write which channels (security/channel-authorization). Open by
// default: a channel with no rule accepts any verified writer, so nothing changes unless a
// deployment asks for a closed channel - and that is what makes the job layer safe to expose, since
// `svc.<name>` is just a channel that any key holder could otherwise publish to.
//
// Rules match with the subscription language, so there is one pattern dialect in the system rather
// than two.

#include <mosaic/core/types.h>
#include <mosaic/match/matcher.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mosaic {

class ChannelRules {
public:
    /// Permit only `writers` to write channels matching `pattern`. False for an invalid pattern.
    bool allow(const std::string& pattern, std::vector<WriterId> writers);

    /// May `writer` write `channel`? True when no rule matches: open is the default, deliberately.
    bool permits(const Channel& channel, const WriterId& writer) const;

    /// The rule that decided a channel, or empty when the channel is open. Used for counting and
    /// echoing.
    std::optional<std::string> rule_for(const Channel& channel) const;

    /// Every rule, in the order added, for an operator to see what is in force at startup.
    const std::vector<std::pair<std::string, std::vector<WriterId>>>& rules() const {
        return rules_;
    }
    std::size_t size() const {
        return rules_.size();
    }

private:
    Matcher matcher_;  // one subscription per rule; the SubId is the rule's index
    std::vector<std::pair<std::string, std::vector<WriterId>>> rules_;
};

}  // namespace mosaic
