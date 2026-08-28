#pragma once

#include <string>

// 通用：向 host:port 的 path POST 一段 JSON，成功（HTTP 200）返回 true
bool PostJson(const std::string& host, int port,
              const std::string& path, const std::string& jsonBody);

// 向 /log 接口上报一条文本消息（封装为 {"message": "..."}）
bool PostLog(const std::string& message);
