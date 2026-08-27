#pragma once
#include <string>

// POST /send -> 发送文本消息
// 请求 body: {"wxid":"目标wxid或群id","content":"消息内容"}
// 返回 JSON: {"code":0,"msg":"ok"} 或错误码
std::string SendMsgResponse(const std::string& body);
