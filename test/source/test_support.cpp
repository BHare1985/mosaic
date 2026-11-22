// Rendering for doctest, kept in the tests: the library does not depend on a test framework, but a
// failing assertion should print the values it compared rather than "CHECK( a == b )".
#include <mosaic/core/record.h>

#include <doctest/doctest.h>
#include <ostream>

using std::ostream;

namespace mosaic {
ostream& operator<<(ostream& out, const Hash& hash) {
    return out << to_hex(hash);
}

ostream& operator<<(ostream& out, const LogKey& key) {
    return out << key.writer << '/' << key.channel;
}

ostream& operator<<(ostream& out, const Record& record) {
    return out << "Record{" << record.writer << '/' << record.channel << " seq=" << record.seq
               << " type=" << record.content_type << " bytes=" << record.payload.size() << '}';
}
}  // namespace mosaic
