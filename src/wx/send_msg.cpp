#include "pch.h"
#include "send_msg.h"
#include "wechat.h"           // GetWeChatWinBase / GetLoginState / WideToUtf8
#include "http_client.h"      // PostLog
#include "json.hpp"           // nlohmann::json
#include "wx_offsets.h"        // 发送相关偏移在下方本地定义（wx_offsets.h 中无）

#include <string>
#include <cstring>

using json = nlohmann::json;

// ── 微信内部字符串结构（参考发送消息.txt 的 WeChatString）──────────
//  +0x00  wchar_t* ptr    指向字符串数据
//  +0x04  int      len     长度（字符数，不含 \0）
//  +0x08  int      maxlen  最大容量
struct WeChatString {
    wchar_t* ptr;
    int      len;
    int      maxlen;
};

// ── 发送消息所需的偏移（参考发送消息.txt，wx_offsets.h 中无）────────
#define WxSendMessageMgr  0x1188ff0   // 获取发送消息管理器
#define WxSendTextMsg      0x1783c10   // 发送文本消息
#define WxFreeChatMsg      0x1199010   // 释放 chat_msg（同 WxReciveMessageCall）

// ── 线程安全：微信发送函数非线程安全，HTTP 并发请求需加锁 ─────────
static CRITICAL_SECTION g_send_cs;
static bool g_send_cs_init = []() {
    InitializeCriticalSectionAndSpinCount(&g_send_cs, 0x400);
    return true;
}();

// ── POD 函数：内联汇编调用微信发送消息 ──────────────────────────
// 不能含 C++ 析构对象（否则 MSVC 报 C2712 不允许 __asm）
static int DoSendText(wchar_t* wxid_buf, wchar_t* msg_buf) {
    // 在栈上构造 WeChatString（参考发送消息.txt）
    WeChatString to_user  = { wxid_buf, (int)wcslen(wxid_buf), (int)wcslen(wxid_buf) };
    WeChatString text_msg = { msg_buf,  (int)wcslen(msg_buf),  (int)wcslen(msg_buf)  };
    wchar_t** msg_pptr = &text_msg.ptr;

    DWORD base = GetWeChatWinBase();
    DWORD send_message_mgr_addr = base + WxSendMessageMgr;
    DWORD send_text_msg_addr    = base + WxSendTextMsg;
    DWORD free_chat_msg_addr    = base + WxFreeChatMsg;

    // chat_msg 缓冲：微信内部用于组装消息结构
    char chat_msg[0x2F0] = { 0 };
    int success = -1;

    __asm {
        PUSHAD
        CALL       send_message_mgr_addr      ; 初始化发送消息管理器
        PUSH       0x0                          ; param5
        PUSH       0x0                          ; param4
        PUSH       0x0                          ; param3
        PUSH       0x1                          ; param2
        PUSH       0x0                          ; param1
        MOV        EAX, msg_pptr                ; &text_msg.ptr（= &text_msg）
        PUSH       EAX                          ; param0: 消息内容结构指针
        LEA        EDX, to_user                 ; EDX = &to_user（目标 wxid 结构）
        LEA        ECX, chat_msg                ; ECX = &chat_msg（this 指针）
        CALL       send_text_msg_addr           ; thiscall(ECX, EDX, stack[6])
        MOV        success, EAX                 ; 捕获返回值
        ADD        ESP, 0x18                     ; 清理 6 × 4 = 24 字节栈参数
        LEA        ECX, chat_msg
        CALL       free_chat_msg_addr           ; 释放 chat_msg
        POPAD
    }
    return success;
}

// UTF-8 → wstring
static std::wstring Utf8ToWString(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}

std::string SendMsgResponse(const std::string& body) {
    // 1) 先校验登录状态
    int state = GetLoginState();
    if (state <= 0) {
        PostLog("发送消息: 未登录 state=" + std::to_string(state));
        json j;
        j["code"]  = 401;
        j["msg"]   = "not logged in";
        j["state"] = state;
        return j.dump();
    }

    // 2) 解析 JSON
    std::string wxid_utf8, content_utf8;
    try {
        json j = json::parse(body);
        wxid_utf8    = j.value("wxid", "");
        content_utf8 = j.value("content", "");
    } catch (...) {
        PostLog("发送消息: JSON 解析失败");
        json j;
        j["code"] = 400;
        j["msg"]  = "invalid json";
        return j.dump();
    }

    if (wxid_utf8.empty() || content_utf8.empty()) {
        PostLog("发送消息: wxid 或 content 为空");
        json j;
        j["code"] = 400;
        j["msg"]  = "wxid and content required";
        return j.dump();
    }

    // 3) UTF-8 → wchar_t 并拷贝到堆（asm 返回前不能释放）
    std::wstring wxid_w    = Utf8ToWString(wxid_utf8);
    std::wstring content_w = Utf8ToWString(content_utf8);

    wchar_t* wxid_buf = new wchar_t[wxid_w.size() + 1];
    wmemcpy(wxid_buf, wxid_w.c_str(), wxid_w.size() + 1);

    wchar_t* msg_buf = new wchar_t[content_w.size() + 1];
    wmemcpy(msg_buf, content_w.c_str(), content_w.size() + 1);

    // 4) 加锁调用微信发送函数
    int success = -1;
    {
        EnterCriticalSection(&g_send_cs);
        success = DoSendText(wxid_buf, msg_buf);
        LeaveCriticalSection(&g_send_cs);
    }

    delete[] wxid_buf;
    delete[] msg_buf;

    // 5) 返回结果
    PostLog("发送消息: wxid=" + wxid_utf8 +
            " success=" + std::to_string(success));

    // 微信发送函数成功时返回消息对象指针（非 0），失败返回 0
    json j;
    if (success != 0 && success != -1) {
        j["code"] = 0;
        j["msg"]  = "ok";
    } else {
        j["code"] = 500;
        j["msg"]  = "send failed";
        j["ret"]  = success;
    }
    return j.dump();
}
