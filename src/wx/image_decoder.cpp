#include "pch.h"
#include "image_decoder.h"
#include "wechat.h"          // GetWeChatWinBase
#include "http_client.h"     // PostLog
#include "config.h"          // cfg::ENABLE_IMAGE_DECODE
#include "doc/offset.txt"     // WxPatchAutoDownloadImage / WxPatchAutoDownloadVideo

#include <fstream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstring>

// ========== 图片类型检测 + XOR 解密（参考 ImageDecoder.cpp） ==========

namespace {

struct ImageTypeInfo {
    int         xorKey = 0;
    const char* extension = nullptr;
    bool        valid = false;
    ImageTypeInfo() = default;
    ImageTypeInfo(int key, const char* ext) : xorKey(key), extension(ext), valid(true) {}
};

// 通过前 2 字节检测图片类型和 XOR 密钥
// 微信 .dat 文件对原始图片逐字节异或了一个 key
// 已知图片头：PNG=8950, JPG=FFD8, BMP=424D, GIF=4749, TIF=4949
static ImageTypeInfo DetectImageType(const char* header)
{
    unsigned char b0 = static_cast<unsigned char>(header[0]);
    unsigned char b1 = static_cast<unsigned char>(header[1]);

    struct Pattern { const char* ext; unsigned char m1, m2; };
    static const Pattern patterns[] = {
        { ".png", 0x89, 0x50 },
        { ".jpg", 0xff, 0xd8 },
        { ".bmp", 0x42, 0x4d },
        { ".gif", 0x47, 0x49 },
        { ".tif", 0x49, 0x49 },
    };

    for (const auto& p : patterns) {
        int xor1 = b0 ^ p.m1;
        int xor2 = b1 ^ p.m2;
        if (xor1 == xor2 && xor1 != 0) {
            return ImageTypeInfo(xor1, p.ext);
        }
    }
    return ImageTypeInfo();
}

// 解码单个 .dat 文件：检测类型 → XOR 解密 → 写入同目录带正确扩展名的文件
static bool DecodeImage(const std::wstring& datPath)
{
    std::ifstream in(datPath, std::ios::binary);
    if (!in.is_open()) {
        PostLog("图片解密: 无法打开 .dat 文件");
        return false;
    }

    char header[2] = {0};
    in.read(header, 2);
    if (in.gcount() < 2) {
        PostLog("图片解密: 文件太小，无法识别类型");
        return false;
    }

    ImageTypeInfo info = DetectImageType(header);
    if (!info.valid) {
        PostLog("图片解密: 无法识别图片类型或密钥");
        return false;
    }

    // 输出路径：把 .dat 替换为检测到的扩展名
    std::wstring outPath = datPath;
    size_t pos = outPath.rfind(L".dat");
    if (pos != std::wstring::npos && pos == outPath.length() - 4) {
        outPath = outPath.substr(0, pos);
    }
    // char* → wstring 扩展名
    for (const char* c = info.extension; *c; ++c)
        outPath.push_back(static_cast<wchar_t>(*c));

    // 从头开始读，逐字节 XOR 解密
    in.clear();
    in.seekg(0, std::ios::beg);

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        PostLog("图片解密: 无法创建输出文件");
        return false;
    }

    // 批量读写（比逐字节快）
    char buf[8192];
    int total = 0;
    while (in) {
        in.read(buf, sizeof(buf));
        std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i)
            buf[i] ^= static_cast<char>(info.xorKey);
        out.write(buf, n);
        total += static_cast<int>(n);
    }
    in.close();
    out.close();

    // UTF-8 化输出路径用于日志
    std::string utf8Path;
    int need = WideCharToMultiByte(CP_UTF8, 0, outPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (need > 0) {
        utf8Path.resize(need - 1);
        WideCharToMultiByte(CP_UTF8, 0, outPath.c_str(), -1, &utf8Path[0], need, nullptr, nullptr);
    }
    PostLog(std::string("图片解密成功: ") + utf8Path +
            " (size=" + std::to_string(total) + ", key=" + std::to_string(info.xorKey) + ")");
    return true;
}

// ========== 工作线程池（参考 ImageDecoder.cpp workerThread） ==========

static bool                    g_stop = false;
static std::queue<std::wstring> g_queue;
static std::mutex              g_mutex;
static std::condition_variable g_cv;
static std::vector<std::thread> g_workers;

static void WorkerThread()
{
    while (true) {
        std::wstring datPath;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, [] { return !g_queue.empty() || g_stop; });
            if (g_stop && g_queue.empty()) return;
            datPath = g_queue.front();
            g_queue.pop();
        }

        // 等待文件写完（微信可能还在下载/写入 .dat）
        bool ready = false;
        for (int retry = 0; retry < 15 && !g_stop; ++retry) {
            HANDLE hFile = CreateFileW(datPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER size;
                if (GetFileSizeEx(hFile, &size) && size.QuadPart > 0) {
                    ready = true;
                    CloseHandle(hFile);
                    break;
                }
                CloseHandle(hFile);
            }
            Sleep(1000);
        }

        if (!ready) {
            PostLog("图片解密: 等待 15 秒后 .dat 文件仍未就绪，跳过");
            continue;
        }

        DecodeImage(datPath);
    }
}

} // namespace

// ========== 对外接口 ==========

void PatchAutoDownload()
{
    DWORD addr = GetWeChatWinBase() + WxPatchAutoDownloadImage;
    DWORD oldProt = 0;
    if (VirtualProtect((LPVOID)addr, 2, PAGE_EXECUTE_READWRITE, &oldProt)) {
        *(BYTE*)addr       = 0x6A;  // PUSH
        *(BYTE*)(addr + 1) = 0x01;  // 1（原来是 00 = 不自动下载）
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)addr, 2);
        VirtualProtect((LPVOID)addr, 2, oldProt, &oldProt);
        PostLog("图片自动下载补丁: 已启用");
    } else {
        PostLog("图片自动下载补丁: VirtualProtect 失败");
    }
}

void PatchAutoDownloadVideo()
{
    DWORD addr = GetWeChatWinBase() + WxPatchAutoDownloadVideo;
    DWORD oldProt = 0;
    if (VirtualProtect((LPVOID)addr, 2, PAGE_EXECUTE_READWRITE, &oldProt)) {
        *(BYTE*)addr       = 0x6A;
        *(BYTE*)(addr + 1) = 0x01;
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)addr, 2);
        VirtualProtect((LPVOID)addr, 2, oldProt, &oldProt);
        PostLog("视频自动下载补丁: 已启用");
    } else {
        PostLog("视频自动下载补丁: VirtualProtect 失败");
    }
}

void InitImageDecoder()
{
    if (!g_workers.empty()) return;  // 已初始化
    g_stop = false;
    for (int i = 0; i < 2; ++i)
        g_workers.emplace_back(WorkerThread);
    PostLog("图片解码器: 2 个工作线程已启动");
}

void ShutdownImageDecoder()
{
    if (g_workers.empty()) return;
    g_stop = true;
    g_cv.notify_all();
    for (auto& w : g_workers) {
        if (w.joinable()) w.join();
    }
    g_workers.clear();
    // 清空队列
    std::queue<std::wstring> empty;
    std::swap(g_queue, empty);
    PostLog("图片解码器: 工作线程已停止");
}

void DecodeImageAsync(const std::wstring& datPath)
{
    if (datPath.empty()) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.push(datPath);
    }
    g_cv.notify_one();
}
