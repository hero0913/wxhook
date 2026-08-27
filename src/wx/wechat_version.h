#pragma once

#include <string>

// 微信文件版本（对应 VS_FIXEDFILEINFO 的四段）
struct WeChatVersion {
    int major = 0;
    int minor = 0;
    int build = 0;
    int revision = 0;
    bool valid = false; // 是否成功读取

    // "主.次.构建.修订"
    std::string ToString() const;
};

// 读取已加载 WeChatWin.dll 的文件版本
WeChatVersion GetWeChatVersion();

// 实际版本是否与期望字符串（"主.次.构建.修订"）一致
bool IsVersionMatch(const WeChatVersion& actual, const std::string& expected);
