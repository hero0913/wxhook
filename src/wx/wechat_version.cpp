#include "pch.h"
#include "wechat_version.h"

#include <windows.h>
#include <vector>
#pragma comment(lib, "version.lib")

std::string WeChatVersion::ToString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." +
           std::to_string(build) + "." + std::to_string(revision);
}

WeChatVersion GetWeChatVersion() {
    WeChatVersion v;

    // 本 DLL 注入到微信进程后，WeChatWin.dll 已加载，直接取其模块句柄
    HMODULE hMod = GetModuleHandleW(L"WeChatWin.dll");
    if (!hMod) return v;

    wchar_t path[MAX_PATH] = {0};
    if (GetModuleFileNameW(hMod, path, MAX_PATH) == 0) return v;

    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (size == 0) return v;

    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return v;

    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffiLen = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &ffiLen))
        return v;
    if (!ffi || ffiLen < sizeof(VS_FIXEDFILEINFO)) return v;

    v.major    = HIWORD(ffi->dwFileVersionMS);
    v.minor    = LOWORD(ffi->dwFileVersionMS);
    v.build    = HIWORD(ffi->dwFileVersionLS);
    v.revision = LOWORD(ffi->dwFileVersionLS);
    v.valid = true;
    return v;
}

bool IsVersionMatch(const WeChatVersion& actual, const std::string& expected) {
    if (!actual.valid) return false;
    return actual.ToString() == expected;
}
