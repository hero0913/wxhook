#pragma once

#include <string>

// ---------- 登录二维码 URL（直接读偏移，无 Hook） ----------

// GET /qr/url[?path=...] 响应体（JSON 字符串）：
//   不带 path: 只返回 URL（不生成图片）
//   带 path  : 生成二维码 PNG 保存到 path，额外返回 saved_path + saved
//   有二维码: {"code":200,"msg":"ok","url":"...","suffix":"..."[,"saved_path":"...","saved":true]}
//   未生成  : {"code":404,"msg":"no qr url","url":"","suffix":""}
//   读取异常: {"code":500,"msg":"read qr url failed","url":"","suffix":""}
std::string GetQRUrlResponse(const std::string& save_path = "");
