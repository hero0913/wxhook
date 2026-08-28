// dllmain.cpp : 定义 DLL 应用程序的入口点。
// 01 启动流程：起 HTTP 服务 -> 校验微信版本 -> 不匹配上报 /log -> 通过则开始 Hook
#include "pch.h"

#include <windows.h>
#include <string>

#include "config.h"
#include "http_server.h"
#include "http_client.h"
#include "wechat_version.h"
#include "hooks.h"

// 启动主流程（在工作线程中执行，避开 DllMain 加载器锁）
static void RunStartup() {
    // 0. 延迟启动：等待 WeChat 自身 DLL（wmpf_host_export.dll 等）完成初始化
    //    避免 Patch/Hook 改了 WeChatWin.dll 内存后，wmpf_host_export.dll
    //    内部指针仍为 NULL 导致 mg_set_protocol_http_websocket 崩溃
    Sleep(cfg::STARTUP_DELAY_MS);

    // 1. 启动 HTTP 服务
    if (!StartHttpServer(cfg::HTTP_PORT)) {
        // 服务未起，但仍尝试上报（上报客户端独立于服务端）
        PostLog("HTTP 服务启动失败 port=" + std::to_string(cfg::HTTP_PORT));
        return;
    }

    // 2. 校验微信版本
    WeChatVersion ver = GetWeChatVersion();
    if (!IsVersionMatch(ver, cfg::EXPECTED_WX_VERSION)) {
        std::string msg = "微信版本不匹配: 实际=" + ver.ToString() +
                          " 期望=" + cfg::EXPECTED_WX_VERSION +
                          (ver.valid ? "" : "(读取失败)");
        PostLog(msg);
        return; // 版本不对：仅上报，不执行 Hook
    }

    // 3. 版本校验通过，开始 Hook
    StartHooks();
}

static DWORD WINAPI MainThread(LPVOID) {
    RunStartup();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        // DllMain 中不做耗时操作，启动工作线程完成初始化
        HANDLE h = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
        break;
    }
    case DLL_PROCESS_DETACH:
        StopHooks();
        StopHttpServer();
        break;
    }
    return TRUE;
}
