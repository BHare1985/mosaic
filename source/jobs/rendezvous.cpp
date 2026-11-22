#include <mosaic/jobs/rendezvous.h>

#include <algorithm>

using std::move;
using std::size_t;
using std::sort;
using std::string;
using std::uint64_t;
using std::vector;

namespace mosaic {

vector<NodeId> rendezvous_rank(const string& job_id, const vector<NodeId>& workers,
                               const Hasher& hasher) {
    struct Scored {
        uint64_t score;
        NodeId worker;
    };
    vector<Scored> scored;
    scored.reserve(workers.size());
    for (const auto& worker : workers) {
        Bytes in(job_id.begin(), job_id.end());
        in.push_back(0);
        in.insert(in.end(), worker.begin(), worker.end());
        const Hash h = hasher.hash(in);
        uint64_t score = 0;
        for (int i = 0; i < 8; ++i) {
            score = (score << 8) | h.bytes[static_cast<size_t>(i)];
        }
        scored.push_back({score, worker});
    }
    std::ranges::sort(scored, [](const Scored& a, const Scored& b) {
        return a.score != b.score ? a.score > b.score : a.worker < b.worker;
    });
    vector<NodeId> out;
    out.reserve(scored.size());
    for (auto& s : scored) {
        out.push_back(std::move(s.worker));
    }
    return out;
}

}  // namespace mosaic
