#pragma once

#include <string>

// 全局配置：端口、期望微信版本、/log 上报地址、/message 转发地址
namespace cfg {
// HTTP 服务绑定信息
static constexpr const char* HTTP_HOST = "0.0.0.0";
static constexpr int  HTTP_PORT = 8888;

// 期望的微信版本，格式：主.次.构建.修订
static constexpr const char* EXPECTED_WX_VERSION = "3.9.12.56";

// 外部 /log 接收端（DLL 通过 HTTP POST 上报异常/关键事件至此）
static constexpr const char* LOG_HOST = "127.0.0.1";
static constexpr int  LOG_PORT = 9999;
static constexpr const char* LOG_PATH = "/log";

// 03 接收消息：Hook 收到的微信消息异步转发到该 HTTP 接口
// （与 tools/receiver.py 的 POST /message 端点对齐）
static constexpr const char* MSG_HOST = "127.0.0.1";
static constexpr int  MSG_PORT = 9999;
static constexpr const char* MSG_PATH = "/message";

// Hook 总开关（StartHooks/StopHooks 内部使用；置 false 则全部 Hook 跳过）
static constexpr bool ENABLE_HOOKS = true;

// 图片落盘解密：置 true 则 Patch 自动下载 + XOR 解密 .dat → .png/.jpg/...
static constexpr bool ENABLE_IMAGE_DECODE = true;

// 语音落盘保存：置 true 则 Hook 语音数据并保存 .silk 到 C:/VoiceLogs/
static constexpr bool ENABLE_VOICE_SAVE = true;

// 注入后延迟启动（毫秒）：等待 WeChat 自身 DLL（如 wmpf_host_export.dll）
// 完成初始化后再起 HTTP 服务 + 安装 Hook，避免时序冲突导致访问违例
static constexpr DWORD STARTUP_DELAY_MS = 3000;
} // namespace cfg
