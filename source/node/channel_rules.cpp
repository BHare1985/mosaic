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
    const auto id = matcher_.add(pattern);
    if (!id)
        return false;
    if (rules_.size() <= *id)
        rules_.resize(*id + 1, {pattern, {}});
    rules_[*id] = {pattern, move(writers)};
    return true;
}

bool ChannelRules::permits(const Channel& channel, const WriterId& writer) const {
    // A channel may match several rules; each one is a statement about who may write there, so the
    // first matching rule that does not name the writer refuses. Anything unmatched is open.
    for (const auto id : matcher_.match(channel)) {
        if (id >= rules_.size())
            continue;
        const auto& permitted = rules_[id].second;
        if (find(permitted.begin(), permitted.end(), writer) != permitted.end())
            return true;
        return false;
    }
    return true;
}

optional<string> ChannelRules::rule_for(const Channel& channel) const {
    const auto hits = matcher_.match(channel);
    if (hits.empty() || hits.front() >= rules_.size())
        return nullopt;
    return rules_[hits.front()].first;
}

}  // namespace mosaic
