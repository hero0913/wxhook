#include "pch.h"
#include "voice_msg.h"
#include "wechat.h"          // GetWeChatWinBase
#include "http_client.h"     // PostLog
#include "config.h"          // cfg::ENABLE_VOICE_SAVE
#include "doc/offset.txt"    // WxReciveVoiceData / WxReciveVoiceDataCall

#include <string>
#include <cstring>
#include <cstdlib>

namespace {

// ---------- Hook 状态 & 原指令备份 ----------
static bool  g_installed = false;
static DWORD g_voice_target = 0;       // base + WxReciveVoiceData
static DWORD g_voice_over_call = 0;    // base + WxReciveVoiceDataCall
static DWORD g_voice_ret_addr = 0;     // g_voice_target + 5
static BYTE  g_voice_orig_bytes[16] = {0};

// ---------- inline hook 底层（与 receive_msg.cpp 相同模式，各自独立备份） ----------
static bool Write5ByteJump(DWORD target, DWORD to)
{
    if (!target || !to) return false;
    DWORD old_protect = 0;
    if (!VirtualProtect((LPVOID)target, 5, PAGE_EXECUTE_READWRITE, &old_protect))
        return false;
    if (g_voice_orig_bytes[0] == 0 && g_voice_orig_bytes[1] == 0 &&
        g_voice_orig_bytes[2] == 0 && g_voice_orig_bytes[3] == 0 && g_voice_orig_bytes[4] == 0) {
        memcpy(g_voice_orig_bytes, (LPCVOID)target, 5);
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
    memcpy((LPVOID)target, g_voice_orig_bytes, 5);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)target, 5);
    VirtualProtect((LPVOID)target, 5, old_protect, &old_protect);
    return true;
}

// ---------- 异步保存语音数据（避免阻塞微信语音回调线程） ----------
// 语音数据在 Hook 返回后可能被微信释放，所以必须先拷贝到堆再异步写盘
struct VoiceSaveParam {
    void*   data;     // malloc 的堆拷贝
    DWORD   length;
    int64_t svrid;
};

static DWORD WINAPI SaveVoiceThread(LPVOID lp)
{
    VoiceSaveParam* p = (VoiceSaveParam*)lp;
    if (!p) return 1;

    CreateDirectoryA("C:\\VoiceLogs", NULL);

    char path[MAX_PATH];
    sprintf_s(path, MAX_PATH, "C:\\VoiceLogs\\%lld.silk", (long long)p->svrid);

    // 已存在则跳过（避免重复保存）
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        PostLog("语音保存: 文件已存在，跳过");
        free(p->data);
        delete p;
        return 0;
    }

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hFile, p->data, p->length, &written, NULL);
        CloseHandle(hFile);
        PostLog(std::string("语音保存成功: ") + path +
                " (size=" + std::to_string(p->length) + ")");
    } else {
        PostLog("语音保存: CreateFileA 失败");
    }

    free(p->data);
    delete p;
    return 0;
}

// __cdecl：被裸函数跳板通过 CALL 调用
// 参数顺序（右→左入栈）：data, length, svrLow, svrHigh
// 调用后由跳板 ADD ESP, 16 清理
void __cdecl SaveVoiceFile(void* data, DWORD length, DWORD svrLow, DWORD svrHigh)
{
    if (!data || !length) return;

    // 拷贝语音数据到堆（原始缓冲区在 Hook 返回后可能失效）
    void* copy = malloc(length);
    if (!copy) return;
    memcpy(copy, data, length);

    int64_t svrid = ((int64_t)svrHigh << 32) | svrLow;

    VoiceSaveParam* param = new VoiceSaveParam();
    param->data   = copy;
    param->length = length;
    param->svrid  = svrid;

    HANDLE h = CreateThread(nullptr, 0, SaveVoiceThread, param, 0, nullptr);
    if (h) CloseHandle(h);
    else { free(copy); delete param; }
}

// ---------- __declspec(naked) 跳板（参考 doc/图片语音落盘.txt） ----------
// EDI 寄存器指向语音数据结构：
//   [EDI + 0x00] = 数据指针
//   [EDI + 0x08] = 数据长度
//   [EDI + 0x40] = msgSvrID 低32位
//   [EDI + 0x44] = msgSvrID 高32位
__declspec(naked) void VoiceDataTrampoline()
{
    __asm {
        PUSHAD
        MOV    EAX, DWORD PTR [EDI + 0x40]    // msgSvrID 低32位
        MOV    EDX, DWORD PTR [EDI + 0x44]    // msgSvrID 高32位
        PUSH   EDX
        PUSH   EAX
        MOV    EAX, DWORD PTR [EDI + 0x08]    // 数据长度
        MOV    ECX, DWORD PTR [EDI]           // 数据指针
        PUSH   EAX
        PUSH   ECX
        CALL   SaveVoiceFile
        ADD    ESP, 16                        // __cdecl 清理 4 个参数
        POPAD
        CALL   [g_voice_over_call]            // 调用被覆盖的原函数
        JMP    [g_voice_ret_addr]             // 跳回原函数入口 + 5
    }
}

} // namespace

// ---------------- 对外入口 ----------------
bool InstallVoiceDataHook()
{
    if (g_installed) return true;

    DWORD base = GetWeChatWinBase();
    if (!base) {
        PostLog("语音 Hook: 无法获取 WeChatWin 基址");
        return false;
    }
    g_voice_target    = base + WxReciveVoiceData;
    g_voice_over_call = base + WxReciveVoiceDataCall;
    g_voice_ret_addr  = g_voice_target + 5;

    if (!Write5ByteJump(g_voice_target, (DWORD)&VoiceDataTrampoline)) {
        PostLog("语音 Hook: 写入入口跳转失败");
        return false;
    }
    g_installed = true;
    PostLog("语音 Hook 安装成功");
    return true;
}

void UninstallVoiceDataHook()
{
    if (!g_installed || !g_voice_target) return;
    if (Restore5ByteJump(g_voice_target)) {
        PostLog("语音 Hook 卸载成功");
    } else {
        PostLog("语音 Hook: 卸载失败（原字节恢复失败）");
    }
    g_installed = false;
}
