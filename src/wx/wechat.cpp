#include "pch.h"
#include "wechat.h"
#include <ShlObj.h>

DWORD GetWeChatWinBase() {
    return (DWORD)GetModuleHandleW(L"WeChatWin.dll");
}

int GetLoginState() {
    DWORD base = GetWeChatWinBase();
    if (!base) return -1;

    // 来自 doc/获取登录状态.txt 示例
    DWORD account_service_addr = base + 0x1198f80;
    DWORD service_addr = 0;

    __try {
        // 无参调用，取 EAX 为 service 指针（等价示例中的 PUSHAD/CALL/POPAD）
        using FuncType = DWORD (*)();
        FuncType f = reinterpret_cast<FuncType>(account_service_addr);
        service_addr = f();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }

    if (!service_addr) return -1;

    DWORD state = 0;
    __try {
        state = *(DWORD*)(service_addr + 0x510);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return (int)state;
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len - 1, '\0'); // 去掉末尾 \0
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, nullptr, nullptr);
    return s;
}

std::wstring GetWeChatCachePath() {
    wchar_t szPath[MAX_PATH] = { 0 };
    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, szPath);
    if (SUCCEEDED(hr)) {
        return std::wstring(szPath);
    }
    return L"";
}
