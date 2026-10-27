#pragma once

#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace zx_web_security {
inline std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
inline std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
inline bool is_control_path(const std::string& path) {
    return path.rfind("/toggle?", 0) == 0 ||
        path.rfind("/camera-toggle?", 0) == 0 ||
        path.rfind("/inference-all?", 0) == 0 ||
        path.rfind("/camera-all?", 0) == 0;
}
// Requiring JSON prevents browser cross-origin simple requests from changing state.
inline int control_request_status(const std::string& method, const std::string& request) {
    if (method != "POST") return 405;
    std::size_t position = request.find("\r\n");
    if (position == std::string::npos) return 415;
    position += 2;
    bool json = false;
    while (position < request.size()) {
        const auto end = request.find("\r\n", position);
        if (end == std::string::npos || end == position) break;
        const std::string line = request.substr(position, end - position);
        const auto colon = line.find(':');
        if (colon != std::string::npos && lower(trim(line.substr(0, colon))) == "content-type") {
            std::string type = lower(trim(line.substr(colon + 1)));
            type = trim(type.substr(0, type.find(';')));
            if (type != "application/json") return 415;
            json = true;
        }
        position = end + 2;
    }
    return json ? 0 : 415;
}
inline in_addr parse_bind_address(const std::string& value) {
    in_addr address{};
    if (inet_pton(AF_INET, value.c_str(), &address) != 1)
        throw std::invalid_argument("--bind requires a numeric IPv4 address");
    return address;
}
inline std::string escape_json(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else out += static_cast<char>(c);
        }
    }
    return out;
}
inline std::string escape_html(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c;
        }
    }
    return out;
}
}  // namespace zx_web_security
