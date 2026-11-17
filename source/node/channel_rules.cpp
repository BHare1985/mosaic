#include <mosaic/node/channel_rules.h>

#include <algorithm>

namespace mosaic {

bool ChannelRules::allow(const std::string& pattern, std::vector<WriterId> writers)
{
    const auto id = matcher_.add(pattern);
    if (!id)
        return false;
    if (rules_.size() <= *id)
        rules_.resize(*id + 1, { pattern, { } });
    rules_[*id] = { pattern, std::move(writers) };
    return true;
}

bool ChannelRules::permits(const Channel& channel, const WriterId& writer) const
{
    // A channel may match several rules; each one is a statement about who may write there, so the first
    // matching rule that does not name the writer refuses. Anything unmatched is open.
    for (const auto id : matcher_.match(channel)) {
        if (id >= rules_.size())
            continue;
        const auto& permitted = rules_[id].second;
        if (std::find(permitted.begin(), permitted.end(), writer) != permitted.end())
            return true;
        return false;
    }
    return true;
}

std::optional<std::string> ChannelRules::rule_for(const Channel& channel) const
{
    const auto hits = matcher_.match(channel);
    if (hits.empty() || hits.front() >= rules_.size())
        return std::nullopt;
    return rules_[hits.front()].first;
}

} // namespace mosaic
