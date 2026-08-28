#include "pch.h"
#include "http_client.h"
#include "config.h"

#include "httplib.h"
#include <string>

namespace {
// 简单 JSON 字符串转义
std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}
} // namespace

bool PostJson(const std::string& host, int port,
              const std::string& path, const std::string& jsonBody) {
    httplib::Client cli(host, port);
    // 短超时兜底：receiver 卡死时最多阻塞 1 秒，不拖死调用方
    // （尤其是 Hook 回调线程，绝不能长时间阻塞）
    cli.set_connection_timeout(1, 0);
    cli.set_read_timeout(1, 0);
    cli.set_write_timeout(1, 0);
    auto res = cli.Post(path, jsonBody, "application/json");
    return res && res->status == 200;
}

bool PostLog(const std::string& message) {
    std::string body = "{\"message\":\"" + EscapeJson(message) + "\"}";
    return PostJson(cfg::LOG_HOST, cfg::LOG_PORT, cfg::LOG_PATH, body);
}
