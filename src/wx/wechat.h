#pragma once

#include <string>
#include <windows.h>

// WeChatWin.dll 基址（DLL 注入到微信进程后该模块已加载）
DWORD GetWeChatWinBase();

// 登录状态（参考 doc/获取登录状态.txt）：
//   调用 base+0x1198f80 取 account_service 指针，读取其 +0x510 的值。
//   返回 -1 表示读取失败/未登录；>0 视为已登录（阈值可按实际调整）。
int GetLoginState();

// 宽字符 -> UTF-8 字符串
std::string WideToUtf8(const wchar_t* w);

// 获取微信缓存目录（我的文档目录，如 C:\Users\xxx\Documents）
// 图片/视频的相对路径需要拼接该目录 + \WeChat Files\ 才是完整路径
std::wstring GetWeChatCachePath();
