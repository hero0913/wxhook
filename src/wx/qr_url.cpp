#include "pch.h"
#include "qr_url.h"
#include "wechat.h"        // GetWeChatWinBase
#include "http_client.h"   // PostLog
#include "wx_offsets.h"    // WxQRUrlOffset
#include "json.hpp"        // nlohmann::json
#include "qrcodegen.hpp"    // Project Nayuki 二维码生成（单头库）

#pragma warning(push)
#pragma warning(disable: 4996)  // stb_image_write 内部使用 sprintf 等 CRT 函数
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"  // PNG 编码（单头库，仅在 qr_url.cpp 内生成实现）
#pragma warning(pop)

#include <string>
#include <cstring>
#include <vector>

using json = nlohmann::json;

namespace {

// 二维码 URL 后缀的固定前缀（参考 doc/登录二维码.txt 的方案说明）
constexpr const char* kQRUrlPrefix = "http://weixin.qq.com/x/";

// POD 缓冲（在 SEH 保护下读取，避免单点崩溃拖垮整个微信进程）
struct QRUrlRaw {
    char suffix[0x100];   // URL 后缀，如 "QdpCp1DjBQlloAAAAAAA"
    bool valid;
    QRUrlRaw() { memset(this, 0, sizeof(*this)); valid = false; }
};

// SEH 保护读取：偏移 0x436C398 处是 ASCII 字符串指针，解引用拿到后缀
// 空指针 / 长度异常（不在 [6, 80]）视为未生成二维码（登录后或残留数据）
bool ReadQRUrlRaw(QRUrlRaw& out) {
    DWORD base = GetWeChatWinBase();
    if (!base) return false;

    __try {
        char* p = *(char**)(base + WxQRUrlOffset);
        if (p) {
            sprintf_s(out.suffix, "%s", p);
        }
        // 合理性校验：长度异常视为未生成（防残留垃圾数据）
        size_t len = strlen(out.suffix);
        if (len < 0x6 || len > 0x80) {
            out.suffix[0] = 0;
        }
        out.valid = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.valid = false;
        return false;
    }
}

// 生成二维码 PNG 并保存到 path（POD 层不做文件 I/O，仅对象层调用 stb）
// scale=8 像素/模块，border=4 模块（QR 标准），容错级别 MEDIUM
bool GenerateQRCodePng(const std::string& url, const std::string& path) {
    if (url.empty() || path.empty()) return false;
    try {
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int qr_size  = qr.getSize();
        const int scale    = 8;
        const int border    = 4 * scale;
        const int img_size  = qr_size * scale + border * 2;

        std::vector<unsigned char> img((size_t)img_size * img_size * 3, 255);
        for (int y = 0; y < qr_size; y++) {
            for (int x = 0; x < qr_size; x++) {
                if (qr.getModule(x, y)) {
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            int px = border + x * scale + dx;
                            int py = border + y * scale + dy;
                            size_t idx = ((size_t)py * img_size + px) * 3;
                            img[idx] = 0; img[idx + 1] = 0; img[idx + 2] = 0;
                        }
                    }
                }
            }
        }
        int ok = stbi_write_png(path.c_str(), img_size, img_size, 3, img.data(), img_size * 3);
        return ok != 0;
    } catch (...) {
        return false;
    }
}

} // namespace

std::string GetQRUrlResponse(const std::string& save_path) {
    QRUrlRaw raw;
    if (!ReadQRUrlRaw(raw)) {
        PostLog("获取二维码 URL: 读取异常 (SEH 捕获)");
        json j;
        j["code"]   = 500;
        j["msg"]    = "read qr url failed";
        j["url"]    = "";
        j["suffix"] = "";
        if (!save_path.empty()) { j["saved_path"] = ""; j["saved"] = false; }
        return j.dump();
    }

    if (raw.suffix[0] == 0) {
        PostLog("获取二维码 URL: 后缀为空（可能已登录或未生成）");
        json j;
        j["code"]   = 404;
        j["msg"]    = "no qr url";
        j["url"]    = "";
        j["suffix"] = "";
        if (!save_path.empty()) { j["saved_path"] = ""; j["saved"] = false; }
        return j.dump();
    }

    std::string suffix(raw.suffix);
    std::string url = std::string(kQRUrlPrefix) + suffix;
    PostLog(std::string("获取二维码 URL: ") + url);

    json j;
    j["code"]   = 200;
    j["msg"]    = "ok";
    j["url"]    = url;
    j["suffix"] = suffix;

    // 携带 path 参数时生成二维码 PNG 保存到指定路径
    if (!save_path.empty()) {
        bool ok = GenerateQRCodePng(url, save_path);
        j["saved_path"] = ok ? save_path : "";
        j["saved"] = ok;
        PostLog(std::string("生成二维码 PNG: ") + (ok ? "成功 " : "失败 ") + save_path);
    }
    return j.dump();
}
