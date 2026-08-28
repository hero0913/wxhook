#include "pch.h"
#include "receive_msg.h"
#include "wechat.h"          // GetWeChatWinBase / WideToUtf8
#include "http_client.h"     // PostLog / PostJson
#include "config.h"          // cfg::MSG_*
#include "image_decoder.h"   // DecodeImageAsync（图片落盘解密）
#include "json.hpp"          // nlohmann::json
#include "wx_offsets.h"      // WxReciveMessage / WxReciveMessageCall / MsgTypeOffset ...

#include <string>
#include <cstring>

using json = nlohmann::json;

namespace {

// Hook 运行状态 & 被覆盖指令的备份
static bool  g_installed = false;
static DWORD g_target_addr = 0;     // GetWeChatWinBase() + WxReciveMessage
static DWORD g_over_call_addr = 0;  // GetWeChatWinBase() + WxReciveMessageCall
static DWORD g_ret_addr = 0;        // g_target_addr + 5
static BYTE  g_orig_bytes[16] = {0};// 被覆盖的原指令备份（前 5 字节）

// ---------- inline hook 底层：修改目标入口的 5 字节为 JMP rel32 ----------
static bool Write5ByteJump(DWORD target, DWORD to)
{
    if (!target || !to) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect((LPVOID)target, 5, PAGE_EXECUTE_READWRITE, &old_protect))
        return false;
    if (g_orig_bytes[0] == 0 && g_orig_bytes[1] == 0 &&
        g_orig_bytes[2] == 0 && g_orig_bytes[3] == 0 && g_orig_bytes[4] == 0) {
        memcpy(g_orig_bytes, (LPCVOID)target, 5);
    }
    BYTE* p = (BYTE*)target;
    p[0] = 0xE9;  // JMP rel32
    *(DWORD*)(&p[1]) = to - target - 5;
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)target, 5);
    VirtualProtect((LPVOID)target, 5, old_protect, &old_protect);
    return true;
}

static bool Restore5ByteJump(DWORD target)
{
    if (!target) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect((LPVOID)target, 5, PAGE_EXECUTE_READWRITE, &old_protect))
        return false;
    memcpy((LPVOID)target, g_orig_bytes, 5);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)target, 5);
    VirtualProtect((LPVOID)target, 5, old_protect, &old_protect);
    return true;
}

// ---------- 微信结构解析辅助：纯 SEH，不引入任何 C++ 对象（避免 C2712） ----------
static DWORD SafeReadDwordPtr(DWORD addr)
{
    if (!addr) return 0;
    DWORD ptr = 0;
    __try {
        ptr = *(DWORD*)addr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return ptr;
}

static const wchar_t* SafeReadWxWideCharPtr(DWORD addr)
{
    if (!addr) return nullptr;
    DWORD ptr = SafeReadDwordPtr(addr);
    if (!ptr) return nullptr;
    const wchar_t* w = nullptr;
    __try {
        w = (const wchar_t*)ptr;
        (void)w[0];  // 探测一下可访问性
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return w;
}

// 无 std::string 版宽串转 UTF-8，直接写入调用方提供的 char 缓冲
static bool WideToUtf8Buf(const wchar_t* w, char* out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!w) return true;
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0) return false;
    if ((size_t)need > out_size) need = (int)out_size - 1;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, need, nullptr, nullptr);
    out[need] = '\0';
    return true;
}

static bool SafeReadWxWideToUtf8Buf(DWORD addr, char* out_buf, size_t buf_size)
{
    const wchar_t* w = SafeReadWxWideCharPtr(addr);
    return WideToUtf8Buf(w, out_buf, buf_size);
}

// 消息类型枚举
enum MsgType : int {
    WxTextMsg            = 1,
    WxImageMsg           = 3,
    WxVoiceMsg           = 34,
    WxVideoMsg           = 43,
    WxEmojiMsg           = 47,
    WxLocationMsg        = 48,
    WxAppMsg             = 49,
    WxSysSyncMsg         = 51,
    WxVoipMsg            = 50,
    WxRecallNotification = 10000,
};

// ---------- POD 原始字段缓冲（SEH 保护读取，全程无 C++ 对象） ----------
struct RawMsg {
    int      msgtype;
    int      isphonemsg;
    uint64_t msgid;
    char     wxid[512];
    char     sender[512];
    char     source[512];
    char     content[2048];
    char     filepath[1024];
    bool     ok;
    bool     skip;  // WxSysSyncMsg 静默跳过
};

// 本函数只包含 POD + __try，绝不引入 std::string 等有析构的对象（强制规避 C2712）
static bool ReadRawMsgPod(DWORD msg_base, RawMsg& raw)
{
    if (!msg_base) return false;
    memset(&raw, 0, sizeof(raw));

    __try {
        raw.msgtype    = *(int*)(msg_base + MsgTypeOffset);
        raw.isphonemsg = *(int*)(msg_base + IsPhoneMsg);
        raw.msgid      = *(uint64_t*)(msg_base + 0x30);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (raw.msgtype == WxSysSyncMsg) {
        raw.skip = true;
        return false;
    }

    __try {
        SafeReadWxWideToUtf8Buf(msg_base + WxidOffset,          raw.wxid,    sizeof(raw.wxid));
        SafeReadWxWideToUtf8Buf(msg_base + GroupMsgSenderOffset, raw.sender,  sizeof(raw.sender));
        SafeReadWxWideToUtf8Buf(msg_base + MsgSourceOffset,     raw.source,  sizeof(raw.source));
        SafeReadWxWideToUtf8Buf(msg_base + MsgContentOffset,    raw.content, sizeof(raw.content));

        switch (raw.msgtype) {
        case WxImageMsg:
            SafeReadWxWideToUtf8Buf(msg_base + ImagePathOffset, raw.filepath, sizeof(raw.filepath));
            break;
        case WxVideoMsg:
            SafeReadWxWideToUtf8Buf(msg_base + VideoPathOffset, raw.filepath, sizeof(raw.filepath));
            break;
        case WxAppMsg:
            if (raw.content[0] != '\0' &&
                (strstr(raw.content, "<type>5</type>")  != nullptr ||
                 strstr(raw.content, "<type>33</type>") != nullptr)) {
                SafeReadWxWideToUtf8Buf(msg_base + XmlImagePathOffset, raw.filepath, sizeof(raw.filepath));
            } else if (raw.content[0] != '\0' &&
                       strstr(raw.content, "<type>6</type>") != nullptr) {
                SafeReadWxWideToUtf8Buf(msg_base + FilePathOffset,     raw.filepath, sizeof(raw.filepath));
            } else {
                SafeReadWxWideToUtf8Buf(msg_base + XmlImagePathOffset, raw.filepath, sizeof(raw.filepath));
            }
            break;
        default:
            break;
        }
        if (raw.filepath[0] == '\0') {
            strncpy_s(raw.filepath, sizeof(raw.filepath), "NULL", _TRUNCATE);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    raw.ok = true;
    return true;
}

// 访问性预检：独立小函数，纯 __try，无 C++ 对象
static bool ProbeMsgBase(DWORD msg_base)
{
    if (!msg_base) return false;
    __try {
        (void)*(volatile DWORD*)(msg_base + MsgTypeOffset);
        (void)*(volatile DWORD*)(msg_base + MsgContentOffset);
        (void)*(volatile DWORD*)(msg_base + WxidOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// ---------- 解析 & 上报：此处允许使用 C++ 对象 ----------
struct PostMsgParam {
    int         msgtype;
    int         isphonemsg;
    uint64_t    msgid;
    std::string wxid;
    std::string sender;
    std::string source;
    std::string content;
    std::string filepath;
};

static DWORD WINAPI PostMsgThread(LPVOID lp)
{
    PostMsgParam* p = (PostMsgParam*)lp;
    if (!p) return 1;
    try {
        // 日志也放到异步线程里，避免阻塞微信消息接收回调
        PostLog(std::string("接收消息: type=") + std::to_string(p->msgtype) +
                " wxid=" + p->wxid +
                (p->sender.empty() ? std::string() : std::string(" sender=") + p->sender));

        // 图片消息(type=3)：把 .dat 路径扔进异步解码队列（非阻塞）
        std::string fullpathUtf8;  // 完整路径的 UTF-8 版，用于 JSON
        if (p->msgtype == WxImageMsg && !p->filepath.empty() && p->filepath != "NULL") {
            // UTF-8 → wstring（filepath 是相对路径）
            int need = MultiByteToWideChar(CP_UTF8, 0, p->filepath.c_str(), -1, nullptr, 0);
            if (need > 0) {
                std::wstring relPath(need - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, p->filepath.c_str(), -1, &relPath[0], need);
                // 相对路径 → 完整路径：缓存目录 + \WeChat Files\ + 相对路径
                std::wstring fullPath = GetWeChatCachePath() + L"\\WeChat Files\\" + relPath;
                DecodeImageAsync(fullPath);
                // 完整路径 → UTF-8（JSON 里带上，方便接收端找解密后的图片）
                fullpathUtf8 = WideToUtf8(fullPath.c_str());
            }
        }

        json j;
        j["msgtype"]     = p->msgtype;
        j["isphonemsg"]  = p->isphonemsg;
        j["msgid"]       = p->msgid;
        j["wxid"]        = p->wxid;
        j["sender"]      = p->sender;
        j["source"]      = p->source;
        j["content"]     = p->content;
        j["filepath"]    = p->filepath;
        j["fullpath"]    = fullpathUtf8;
        j["isgroup"]     = !p->sender.empty();
        std::string body = j.dump();
        PostJson(cfg::MSG_HOST, cfg::MSG_PORT, cfg::MSG_PATH, body);
    } catch (...) {
        PostLog("接收消息: 线程 POST /message 异常");
    }
    delete p;
    return 0;
}

static void DispatchMsgAsync(PostMsgParam* param)
{
    if (!param) return;
    HANDLE h = CreateThread(nullptr, 0, PostMsgThread, param, 0, nullptr);
    if (h) CloseHandle(h);
    else   delete param;
}

// 封装 std::string + DispatchMsgAsync：无 __try，可安心使用 C++ 对象
// 注意：本函数跑在微信消息接收回调线程上，绝不碰任何网络 I/O
// （PostLog/PostJson 全部在 PostMsgThread 异步线程里做）
static void ProcessParsedMsg(const RawMsg& raw)
{
    PostMsgParam* out = new PostMsgParam();
    out->msgtype     = raw.msgtype;
    out->isphonemsg  = raw.isphonemsg;
    out->msgid       = raw.msgid;
    out->wxid        = raw.wxid[0]     ? raw.wxid     : std::string("NULL");
    out->sender      = raw.sender;
    out->source      = raw.source;
    out->content     = raw.content;
    out->filepath    = raw.filepath;

    DispatchMsgAsync(out);
}

// 轻量异步日志：把 PostLog 扔到独立线程，绝不阻塞微信回调线程
struct LogParam {
    char msg[512];
};

static DWORD WINAPI PostLogThread(LPVOID lp)
{
    LogParam* p = (LogParam*)lp;
    if (p) {
        PostLog(p->msg);
        delete p;
    }
    return 0;
}

static void PostLogAsync(const char* msg)
{
    LogParam* p = new LogParam();
    strncpy_s(p->msg, sizeof(p->msg), msg, _TRUNCATE);
    HANDLE h = CreateThread(nullptr, 0, PostLogThread, p, 0, nullptr);
    if (h) CloseHandle(h);
    else   delete p;
}

// __stdcall 回调：本函数**不包含任何** __try / __except（避免对象展开冲突 C2712）
// 所有 SEH 操作都在独立的 POD 小函数里完成
// 本函数跑在微信消息接收回调线程上，绝不碰任何网络 I/O
void __stdcall OnRecvMessageFromStack(DWORD msg_base)
{
    if (!ProbeMsgBase(msg_base)) return;

    RawMsg raw;
    if (!ReadRawMsgPod(msg_base, raw)) {
        if (raw.skip) return;  // WxSysSyncMsg 静默跳过
        PostLogAsync("接收消息: 读取原始字段失败 (SEH 捕获)");
        return;
    }

    ProcessParsedMsg(raw);
}

// ---------- __declspec(naked) 跳板：保存现场 -> 调 C++ 处理 -> ----------
// ---------- 调用被覆盖 call -> 跳到返回地址                     ----------
__declspec(naked) void RecvMessageTrampoline()
{
    __asm {
        PUSHAD
        PUSH    ECX                     // ECX = msg 结构指针（参考示例 PUSH ECX）
        CALL    OnRecvMessageFromStack
        POPAD
        CALL    [g_over_call_addr]      // 调用被覆盖的 call（WxReciveMessageCall）
        JMP     [g_ret_addr]            // 跳回原函数入口 + 5
    }
}

} // namespace

// ---------------- 对外入口 ----------------
bool InstallReceiveMessageHook()
{
    if (g_installed) return true;

    DWORD base = GetWeChatWinBase();
    if (!base) {
        PostLog("接收消息 Hook: 无法获取 WeChatWin 基址");
        return false;
    }
    g_target_addr    = base + WxReciveMessage;
    g_over_call_addr = base + WxReciveMessageCall;
    g_ret_addr       = g_target_addr + 5;

    if (!Write5ByteJump(g_target_addr, (DWORD)&RecvMessageTrampoline)) {
        PostLog("接收消息 Hook: 写入入口跳转失败");
        return false;
    }
    g_installed = true;
    PostLog("接收消息 Hook 安装成功");
    return true;
}

void UninstallReceiveMessageHook()
{
    if (!g_installed || !g_target_addr) return;
    if (Restore5ByteJump(g_target_addr)) {
        PostLog("接收消息 Hook 卸载成功");
    } else {
        PostLog("接收消息 Hook: 卸载失败（原字节恢复失败）");
    }
    g_installed = false;
}
