#include "pch.h"
#include "hooks.h"
#include "receive_msg.h"   // InstallReceiveMessageHook / UninstallReceiveMessageHook
#include "voice_msg.h"    // InstallVoiceDataHook / UninstallVoiceDataHook
#include "image_decoder.h" // PatchAutoDownload / InitImageDecoder / ShutdownImageDecoder
#include "config.h"        // cfg::ENABLE_HOOKS / cfg::ENABLE_IMAGE_DECODE / cfg::ENABLE_VOICE_SAVE
#include "http_client.h"   // PostLog

// 注：目前采用 __declspec(naked) + 5-byte inline hook 方案，
// 无 Detours.lib 依赖，无需 Detours 事务。

bool StartHooks() {
    if (!cfg::ENABLE_HOOKS) {
        PostLog("Hook 全局开关已关闭，跳过 StartHooks");
        return true;
    }
    bool ok = true;

    // 图片落盘解密：先 Patch 自动下载，再启动解码工作线程
    if (cfg::ENABLE_IMAGE_DECODE) {
        PatchAutoDownload();
        PatchAutoDownloadVideo();
        InitImageDecoder();
    }

    // 语音数据 Hook：拦截语音数据并保存 .silk
    if (cfg::ENABLE_VOICE_SAVE) {
        ok = InstallVoiceDataHook() && ok;
    }

    ok = InstallReceiveMessageHook() && ok;
    // 发送消息无需 Hook，通过 HTTP POST /send 直接调用
    if (ok) PostLog("StartHooks: 全部 Hook 安装完成");
    else    PostLog("StartHooks: 部分 Hook 安装失败");
    return ok;
}

void StopHooks() {
    if (!cfg::ENABLE_HOOKS) return;
    UninstallReceiveMessageHook();

    // 卸载语音数据 Hook
    if (cfg::ENABLE_VOICE_SAVE) {
        UninstallVoiceDataHook();
    }

    // 停止图片解码工作线程
    if (cfg::ENABLE_IMAGE_DECODE) {
        ShutdownImageDecoder();
    }

    PostLog("StopHooks: 全部 Hook 已卸载");
}
