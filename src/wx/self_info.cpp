#include "pch.h"
#include "self_info.h"
#include "wechat.h"
#include "http_client.h"      // PostLog
#include "doc/offset.txt"      // WxID / WxCount / ... 偏移宏
#include "json.hpp"            // nlohmann::json

#include <string>
#include <cstring>

using json = nlohmann::json;

namespace {

// 个人信息原始字段（POD 缓冲，便于在 SEH 保护下读取）
struct SelfInfoRaw {
    char wxcount[0x300];
    char wxid[0x300];
    char nation[0x300];
    char province[0x300];
    char city[0x300];
    char phonenumber[0x300];
    char nickname[0x300];
    wchar_t cachedir[0x300];
    DWORD sex;
    bool valid;
    SelfInfoRaw() { memset(this, 0, sizeof(*this)); valid = false; }
};

// SEH 保护读取，避免单点崩溃导致整个微信进程退出
bool ReadSelfInfoRaw(SelfInfoRaw& out) {
    DWORD base = GetWeChatWinBase();
    if (!base) return false;

    __try {
        // 微信号（为空时上层置 NULL）
        sprintf_s(out.wxcount, "%s", (char*)(base + WxCount));

        // 微信ID：新老号兼容，长度不在 [0x6,0x14] 视为指针
        sprintf_s(out.wxid, "%s", (char*)(base + WxID));
        if (strlen(out.wxid) < 0x6 || strlen(out.wxid) > 0x14) {
            sprintf_s(out.wxid, "%s", (char*)(*(DWORD*)(base + WxID)));
        }

        sprintf_s(out.nation,       "%s", (char*)(base + WxNation));
        sprintf_s(out.province,     "%s", (char*)(base + WxProvince));
        sprintf_s(out.city,        "%s", (char*)(base + WxCity));
        sprintf_s(out.phonenumber, "%s", (char*)(base + WxPhoneNumber));

        // 昵称：偏移 +0x14 处为 0xF 时按字符串读取，否则按指针解引用
        if (*(DWORD*)(base + WxNickName + 0x14) == 0xF) {
            sprintf_s(out.nickname, "%s", (char*)(base + WxNickName));
        } else {
            sprintf_s(out.nickname, "%s", (char*)(*(DWORD*)(base + WxNickName)));
        }

        // 缓存目录（Unicode 指针）
        swprintf_s(out.cachedir, L"%s", (wchar_t*)(*(DWORD*)(base + WxCacheDir)));

        out.sex = *(DWORD*)(base + WxSex);
        out.valid = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.valid = false;
        return false;
    }
}

} // namespace

std::string GetLoginStateResponse() {
    int state = GetLoginState();
    PostLog("登录状态查询 state=" + std::to_string(state));
    json j;
    j["state"]  = state;
    j["logged"] = (state > 0);
    return j.dump();
}

std::string GetSelfInfoResponse() {
    // 1) 先获取登录状态
    int state = GetLoginState();
    PostLog("获取个人信息: 登录状态 state=" + std::to_string(state));
    if (state <= 0) {
        json j;
        j["code"]  = 401;
        j["msg"]   = "not logged in";
        j["state"] = state;
        return j.dump();
    }

    // 2) 已登录，读取个人信息字段
    SelfInfoRaw raw;
    if (!ReadSelfInfoRaw(raw)) {
        PostLog("获取个人信息: 读取字段异常 (SEH 捕获)");
        json j;
        j["code"] = 500;
        j["msg"]  = "read self info failed";
        return j.dump();
    }

    // 字段字节按 UTF-8 处理（与 /utf-8 一致；参考示例用 UTF8ToUnicode 即说明源为 UTF-8）
    json j;
    j["wxcount"]     = raw.wxcount[0] ? std::string(raw.wxcount) : std::string("NULL");
    j["wxid"]        = std::string(raw.wxid);
    j["nation"]      = std::string(raw.nation);
    j["province"]    = std::string(raw.province);
    j["city"]        = std::string(raw.city);
    j["phonenumber"] = std::string(raw.phonenumber);
    j["nickname"]    = std::string(raw.nickname);
    j["cachedir"]    = WideToUtf8(raw.cachedir);
    switch (raw.sex) {
    case 1:  j["wxsex"] = "男";   break;
    case 2:  j["wxsex"] = "女";   break;
    default: j["wxsex"] = "未设置"; break;
    }

    std::string out;
    try {
        out = j.dump();
    } catch (const std::exception& e) {
        PostLog(std::string("获取个人信息: json dump 失败 ") + e.what());
        json f;
        f["code"] = 500;
        f["msg"]  = std::string("json dump failed: ") + e.what();
        out = f.dump();
    }

    PostLog(std::string("获取个人信息: 读取完成 wxid=") +
            (raw.wxid[0] ? raw.wxid : "NULL"));
    return out;
}
