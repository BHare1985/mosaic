#include <mosaic/core/encoding.h>
#include <mosaic/jobs/job.h>

using std::nullopt;
using std::optional;

namespace mosaic {

Bytes encode_job_request(const JobRequest& request) {
    Bytes out;
    ByteWriter writer(out);
    writer.put_string(request.job_id);
    writer.put_string(request.service);
    writer.put_string(request.reply_channel);
    writer.put_u32(request.timeout_ms);
    writer.put_bytes(request.body);
    return out;
}

optional<JobRequest> decode_job_request(const Bytes& bytes) {
    ByteReader reader(bytes);
    JobRequest request;
    request.job_id = reader.get_string();
    request.service = reader.get_string();
    request.reply_channel = reader.get_string();
    request.timeout_ms = reader.get_u32();
    request.body = reader.get_bytes();
    if (!reader.ok() || !reader.at_end() || request.job_id.empty() || request.reply_channel.empty())
        return nullopt;  // a half-read envelope is not a job
    return request;
}

Bytes encode_job_result(const JobResult& result) {
    Bytes out;
    ByteWriter writer(out);
    writer.put_string(result.job_id);
    writer.put_u8(result.ok ? 1 : 0);
    writer.put_bytes(result.body);
    return out;
}

optional<JobResult> decode_job_result(const Bytes& bytes) {
    ByteReader reader(bytes);
    JobResult result;
    result.job_id = reader.get_string();
    result.ok = reader.get_u8() != 0;
    result.body = reader.get_bytes();
    if (!reader.ok() || !reader.at_end() || result.job_id.empty())
        return nullopt;
    return result;
}

}  // namespace mosaic
