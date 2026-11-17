#pragma once

// The LogStore contract as a reusable artefact (board item STORE-0, spec: store/log-store).
// Every backend - memory, SQLite, segment files - is verified by supplying a factory and nothing else.

#include <functional>
#include <memory>
#include <mosaic/store/log_store.h>

namespace mosaic::test {

using StoreFactory = std::function<std::unique_ptr<LogStore>()>;

void run_store_conformance(const StoreFactory& factory);

} // namespace mosaic::test
