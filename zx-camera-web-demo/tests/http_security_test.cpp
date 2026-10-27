#include "http_security.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace zx_web_security;
    assert(is_control_path("/toggle?id=j7&enabled=0"));
    assert(is_control_path("/camera-all?enabled=0"));
    assert(!is_control_path("/status"));
    const std::string valid = "POST /toggle?id=j7&enabled=0 HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n\r\n";
    assert(control_request_status("POST", valid) == 0);
    assert(control_request_status("GET", valid) == 405);
    assert(control_request_status("OPTIONS", valid) == 405);
    assert(control_request_status("POST", "POST / HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n") == 415);
    assert(control_request_status("POST", "POST / HTTP/1.1\r\nContent-Type: text/plain\r\n\r\nContent-Type: application/json\r\n") == 415);
    assert(control_request_status("POST", "POST / HTTP/1.1\r\n\r\nContent-Type: application/json\r\n") == 415);
    assert(control_request_status("POST", "POST / HTTP/1.1\r\ncontent-type: Application/JSON; charset=utf-8\r\n\r\n") == 0);
    assert(control_request_status("POST", "POST / HTTP/1.1\r\nContent-Type: application/jsonX\r\n\r\n") == 415);
    const auto local = parse_bind_address("127.0.0.1");
    assert(ntohl(local.s_addr) == INADDR_LOOPBACK);
    bool rejected = false;
    try { parse_bind_address("not-an-ip"); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    assert(escape_json("\"\\\n\r\t") == "\\\"\\\\\\n\\r\\t");
    assert(escape_json(std::string(1, '\x01')) == "\\u0001");
    assert(escape_html("<img src=x onerror='attack()'>&\"") == "&lt;img src=x onerror=&#39;attack()&#39;&gt;&amp;&quot;");
    std::cout << "PASS: camera control request validation, loopback binding, JSON and HTML escaping.\n";
}
