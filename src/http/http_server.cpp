#include "pch.h"
#include "http_server.h"
#include "wechat_version.h"
#include "config.h"
#include "self_info.h"        // GetLoginStateResponse / GetSelfInfoResponse
#include "send_msg.h"          // SendMsgResponse

#include "httplib.h"
#include <atomic>
#include <thread>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace {
httplib::Server g_server;
std::thread     g_thread;
std::atomic<bool> g_running{false};

// 构造一段简单 JSON 字符串（仅转义必要字符）
std::string JsonString(const std::string& s) {
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

void RegisterRoutes() {
    // 健康检查 / 当前版本状态
    g_server.Get("/status", [](const httplib::Request&, httplib::Response& res) {
        WeChatVersion v = GetWeChatVersion();
        bool match = IsVersionMatch(v, cfg::EXPECTED_WX_VERSION);
        std::string body = "{\"wx_version\":\"" + JsonString(v.ToString()) +
                           "\",\"expected\":\"" + JsonString(cfg::EXPECTED_WX_VERSION) +
                           "\",\"matched\":" + (match ? "true" : "false") + "}";
        res.set_content(body, "application/json");
    });

    // TODO(02): 注册 获取个人信息 接口
    g_server.Get("/login/state", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetLoginStateResponse(), "application/json");
    });
    g_server.Get("/self/info", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(GetSelfInfoResponse(), "application/json");
    });
    // TODO(03): 注册 接收消息回调 / 接收消息接口
    // 发送文本消息
    g_server.Post("/send", [](const httplib::Request& req, httplib::Response& res) {
        std::string body = req.body;
        if (body.empty() && req.has_param("body")) {
            body = req.get_param_value("body");
        }
        res.set_content(SendMsgResponse(body), "application/json");
    });
}
} // namespace

bool StartHttpServer(int port) {
    if (g_running.load()) return true;

    RegisterRoutes();

    // 先同步绑定端口，便于立即判断是否成功；随后在线程中阻塞监听
    if (!g_server.bind_to_port(cfg::HTTP_HOST, port)) return false;

    g_thread = std::thread([]() {
        g_server.listen_after_bind();
    });

    g_running.store(true);
    return true;
}

void StopHttpServer() {
    g_server.stop();
    if (g_thread.joinable()) g_thread.join();
    g_running.store(false);
}

bool IsHttpServerRunning() {
    return g_running.load();
}
