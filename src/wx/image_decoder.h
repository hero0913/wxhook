#pragma once
#include <string>

// ---------- 图片落盘解密（参考 doc/ImageDecoder.cpp + doc/图片语音落盘.txt） ----------

// 补丁：启用微信自动下载图片/视频到本地（.dat 文件）
void PatchAutoDownload();
void PatchAutoDownloadVideo();

// 解码器生命周期：Init 启动工作线程，Shutdown 停止
void InitImageDecoder();
void ShutdownImageDecoder();

// 异步解码 .dat 图片文件（非阻塞，入队后由工作线程处理）
void DecodeImageAsync(const std::wstring& datPath);
