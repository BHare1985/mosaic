#include <algorithm>
#include <mosaic/jobs/rendezvous.h>

namespace mosaic {

std::vector<NodeId> rendezvous_rank(const std::string& job_id, const std::vector<NodeId>& workers, const Hasher& hasher)
{
    struct Scored {
        std::uint64_t score;
        NodeId worker;
    };
    std::vector<Scored> scored;
    scored.reserve(workers.size());
    for (const auto& worker : workers) {
        Bytes in(job_id.begin(), job_id.end());
        in.push_back(0);
        in.insert(in.end(), worker.begin(), worker.end());
        const Hash h = hasher.hash(in);
        std::uint64_t score = 0;
        for (int i = 0; i < 8; ++i)
            score = (score << 8) | h.bytes[static_cast<std::size_t>(i)];
        scored.push_back({ score, worker });
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.score != b.score ? a.score > b.score : a.worker < b.worker;
    });
    std::vector<NodeId> out;
    out.reserve(scored.size());
    for (auto& s : scored)
        out.push_back(std::move(s.worker));
    return out;
}

} // namespace mosaic
