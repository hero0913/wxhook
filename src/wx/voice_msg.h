#pragma once

// ---------- 语音数据 Hook + .silk 落盘（参考 doc/图片语音落盘.txt） ----------

// 安装语音数据 Hook（5-byte inline hook，与 receive_msg 同方案）
bool InstallVoiceDataHook();

// 卸载语音数据 Hook（恢复原 5 字节）
void UninstallVoiceDataHook();
