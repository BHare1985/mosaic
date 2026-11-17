#include <mosaic/daemon/config.h>

#include <cstdlib>
#include <czmq.h> // zconfig.h is not standalone: it needs czmq's base types
#include <sstream>

namespace mosaic::daemon {
namespace {

    /// The single source of truth for defaults: the parser fills absent settings from here and `--defaults`
    /// renders this table, so documentation cannot drift from behaviour.
    struct Setting {
        const char* section;
        const char* key;
        const char* default_value;
        const char* comment;
    };

    constexpr Setting kSettings[] = {
        { "node", "id", "", "Identity of this node: the name peers address it by" },
        { "node", "store", "", "SQLite file holding the logs this node replicates" },
        { "node", "keys", "", "Keystore written by `Mosaic keygen`" },
        { "node", "listen", "127.0.0.1:6200", "PUB binds here; ROUTER binds on the next port" },
        { "node", "head_interval_ms", "1000", "How often a writer announces its head" },
        { "node", "fetch_batch", "64", "Most records requested by one FETCH" },
        { "node", "fetch_window", "256", "Most records allowed in flight from one peer" },
        { "node", "fetch_timeout_ms", "500", "Presume a FETCH lost after this long" },
        { "node", "max_serve", "1024", "Most records served for one FETCH" },
        { "node", "min_replicas", "0", "Replicas that must acknowledge before a record is released" },
        { "node", "retention_limit", "10000", "Most records retained while waiting for those acknowledgements" },
        { "node", "slow_follower_threshold", "512", "Sequences behind before a follower repairs instead of tailing" },
        { "node", "control_interval_ms", "500", "Spacing for head requests, acknowledgements and request budgets" },
        { "node", "max_record_bytes", "262144", "Largest record this node accepts or publishes" },
        { "node", "max_head_patterns", "64", "Most patterns a single head request may carry" },
        { "node", "max_requests_per_interval", "32", "Requests of one kind served per peer per interval" },
        { "node", "curve", "on", "on or off: encrypt links (turn off only for diagnostics)" },
        { "log", "level", "info", "debug, info or error" },
        { "log", "status_interval_ms", "10000", "How often to print the counter summary" },
        { "peer", "id", "", "Peer identity (repeat the section for each peer)" },
        { "peer", "endpoint", "", "Peer host:port for its PUB; its ROUTER is port+1" },
        { "peer", "curve", "\"\"", "Peer CURVE public key, z85 - quote it, a '#' inside an unquoted value starts a comment" },
        { "follow", "pattern", "", "Channel pattern to replicate (repeat the section per pattern)" },
        { "allow", "pattern", "", "Channel pattern whose writers are restricted (repeat per rule)" },
        { "allow", "writer", "", "Writer identity allowed to write that pattern (repeatable)" },
        { "discover", "cluster", "mosaic", "Scopes discovery: two deployments on one network must not merge" },
        { "discover", "beacon_port", "0", "UDP port to announce on; 0 disables LAN discovery" },

    };

    /// Expand `${NAME}` from the environment. Deliberately ours rather than zconfig's: an unset variable must
    /// be an error naming it, never an empty string silently substituted into a key path.
    bool expand_env(const std::string& in, std::string& out, std::string& missing)
    {
        out.clear();
        std::size_t pos = 0;
        while (pos < in.size()) {
            const auto start = in.find("${", pos);
            if (start == std::string::npos) {
                out.append(in, pos, std::string::npos);
                return true;
            }
            out.append(in, pos, start - pos);
            const auto end = in.find('}', start);
            if (end == std::string::npos) {
                out.append(in, start, std::string::npos); // an unmatched ${ is literal text
                return true;
            }
            const auto name = in.substr(start + 2, end - start - 2);
            const char* value = std::getenv(name.c_str());
            if (value == nullptr) {
                missing = name;
                return false;
            }
            out.append(value);
            pos = end + 1;
        }
        return true;
    }

    bool is_known(const std::string& section, const std::string& key)
    {
        for (const auto& setting : kSettings)
            if (section == setting.section && key == setting.key)
                return true;
        return false;
    }

    bool parse_u32(const std::string& text, std::uint32_t& out)
    {
        if (text.empty())
            return false;
        std::size_t consumed = 0;
        try {
            const auto value = std::stoul(text, &consumed);
            if (consumed != text.size() || value > 0xffffffffull)
                return false;
            out = static_cast<std::uint32_t>(value);
            return true;
        } catch (...) {
            return false;
        }
    }

    struct Walker {
        LoadResult& result;
        std::string section;

        void error(const std::string& where, const std::string& message) { result.errors.push_back({ where, message }); }

        /// Resolve one property value, then hand it to the right field.
        void apply(const std::string& key, const std::string& raw)
        {
            std::string value;
            std::string missing;
            if (!expand_env(raw, value, missing)) {
                error(section + "." + key, "environment variable " + missing + " is not set");
                return;
            }
            auto& config = result.config;
            const auto number = [&](std::uint32_t& field) {
                if (!parse_u32(value, field))
                    error(section + "." + key, "'" + value + "' is not a number");
            };

            if (section == "node") {
                if (key == "id")
                    config.id = value;
                else if (key == "store")
                    config.store = value;
                else if (key == "keys")
                    config.keys = value;
                else if (key == "listen")
                    config.listen = value;
                else if (key == "head_interval_ms")
                    number(config.head_interval_ms);
                else if (key == "fetch_batch")
                    number(config.fetch_batch);
                else if (key == "fetch_window")
                    number(config.fetch_window);
                else if (key == "fetch_timeout_ms")
                    number(config.fetch_timeout_ms);
                else if (key == "max_serve")
                    number(config.max_serve);
                else if (key == "min_replicas")
                    number(config.min_replicas);
                else if (key == "retention_limit")
                    number(config.retention_limit);
                else if (key == "slow_follower_threshold")
                    number(config.slow_follower_threshold);
                else if (key == "control_interval_ms")
                    number(config.control_interval_ms);
                else if (key == "max_record_bytes")
                    number(config.max_record_bytes);
                else if (key == "max_head_patterns")
                    number(config.max_head_patterns);
                else if (key == "max_requests_per_interval")
                    number(config.max_requests_per_interval);
                else if (key == "curve") {
                    if (value == "on")
                        config.curve = true;
                    else if (value == "off")
                        config.curve = false;
                    else
                        error("node.curve", "must be on or off");
                } else
                    unknown(key);
            } else if (section == "log") {
                if (key == "level") {
                    if (value != "debug" && value != "info" && value != "error")
                        error("log.level", "must be debug, info or error");
                    else
                        config.level = value;
                } else if (key == "status_interval_ms")
                    number(config.status_interval_ms);
                else
                    unknown(key);
            } else if (section == "peer") {
                if (config.peers.empty() || config.peers.back().id.empty() == false) {
                    // a new peer section starts when the previous one already has an id
                    if (key == "id")
                        config.peers.push_back({ value, { }, { } });
                }
                if (!config.peers.empty()) {
                    auto& peer = config.peers.back();
                    if (key == "endpoint")
                        peer.endpoint = value;
                    else if (key == "curve")
                        peer.curve_public = value;
                    else if (key != "id")
                        unknown(key);
                }
            } else if (section == "follow") {
                if (key == "pattern")
                    config.follows.push_back(value);
                else
                    unknown(key);
            } else if (section == "discover") {
                if (key == "cluster")
                    config.discover.cluster = value;
                else if (key == "beacon_port") {
                    std::uint32_t port = 0;
                    if (!parse_u32(value, port) || port > 65535)
                        error("discover.beacon_port", "must be a UDP port number (0 disables)");
                    else
                        config.discover.beacon_port = static_cast<std::uint16_t>(port);
                } else
                    unknown(key);
            } else if (section == "allow") {
                if (key == "pattern")
                    config.allow.push_back({ value, { } });
                else if (key == "writer") {
                    if (config.allow.empty())
                        error("allow.writer", "a writer entry appeared before any allow.pattern");
                    else
                        config.allow.back().writers.push_back(value);
                } else
                    unknown(key);
            }
        }

        void unknown(const std::string& key) { error(section + "." + key, "unknown setting"); }

        void walk(zconfig_t* node)
        {
            for (zconfig_t* child = zconfig_child(node); child != nullptr; child = zconfig_next(child)) {
                const std::string name = zconfig_name(child);
                const std::string value = zconfig_value(child) ? zconfig_value(child) : "";
                if (zconfig_child(child) != nullptr) {
                    const std::string outer = section;
                    section = name;
                    walk(child);
                    section = outer;
                    continue;
                }
                if (!is_known(section, name)) {
                    unknown(name);
                    continue;
                }
                apply(name, value);
            }
        }
    };

    std::string trim(const char* value) { return value ? std::string(value) : std::string { }; }

} // namespace

LoadResult load_config(const std::string& path)
{
    LoadResult result;
    zconfig_t* root = zconfig_load(path.c_str());
    if (root == nullptr) {
        result.errors.push_back({ "config", "cannot read " + path });
        return result;
    }

    Walker walker { result, { } };
    // Top-level properties are an error: the file is sections all the way down.
    for (zconfig_t* child = zconfig_child(root); child != nullptr; child = zconfig_next(child)) {
        const std::string name = zconfig_name(child);
        if (zconfig_child(child) == nullptr) {
            result.errors.push_back({ trim(zconfig_name(child)), "properties must live in a section" });
            continue;
        }
        walker.section = name;
        walker.walk(child);
    }
    zconfig_destroy(&root);

    // Required settings, and the shapes that cannot be checked property by property.
    if (result.config.id.empty())
        result.errors.push_back({ "node.id", "required" });
    if (result.config.store.empty())
        result.errors.push_back({ "node.store", "required" });
    if (result.config.keys.empty())
        result.errors.push_back({ "node.keys", "required" });
    for (const auto& peer : result.config.peers) {
        if (peer.id.empty())
            result.errors.push_back({ "peer.id", "required for every peer section" });
        if (peer.endpoint.empty())
            result.errors.push_back({ "peer.endpoint", "required for peer " + peer.id });
        // A Z85 key is exactly 40 characters. Checking it turns a silent truncation into an error: a '#'
        // inside an unquoted ZPL value starts a comment, which is exactly how a peer key gets cut short.
        if (!peer.curve_public.empty() && peer.curve_public.size() != 40)
            result.errors.push_back({ "peer.curve",
                "must be 40 z85 characters, got " + std::to_string(peer.curve_public.size())
                    + " (quote the value: a '#' inside an unquoted ZPL value starts a comment)" });
    }
    for (const auto& rule : result.config.allow)
        if (rule.writers.empty())
            result.errors.push_back({ "allow.writer", "no writers listed for pattern " + rule.pattern });
    if (result.config.discover.enabled() && result.config.discover.cluster.empty())
        result.errors.push_back({ "discover.cluster", "required when discovery is enabled" });
    return result;
}

std::string render_defaults()
{
    std::ostringstream out;
    out << "# Mosaic daemon configuration (ZPL). Generated from the defaults the daemon actually uses.\n"
           "# Fill in node.id, node.store and node.keys; everything else already has the value below.\n";
    std::string current;
    for (const auto& setting : kSettings) {
        const std::string section = setting.section;
        // The repeatable sections are examples rather than properties: emitting them live would create an
        // empty peer or rule and fail validation on a document we printed ourselves.
        const bool example = section == "peer" || section == "follow" || section == "allow";
        if (current != section) {
            current = section;
            out << "\n";
            if (example)
                out << "# " << section << " (repeat this section for each entry)\n";
            else
                out << section << "\n";
        }
        const auto prefix = example ? "#     " : "    ";
        out << prefix << "# " << setting.comment << "\n";
        out << prefix << setting.key << " = " << setting.default_value << "\n";
    }
    return out.str();
}

} // namespace mosaic::daemon
