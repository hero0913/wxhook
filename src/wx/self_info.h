#pragma once

#include <string>

// GET /login/state -> {"state":N,"logged":bool}
std::string GetLoginStateResponse();

// GET /self/info：先校验登录状态，未登录返回 401 JSON；
// 已登录读取个人信息字段并返回 JSON。全程关键点通过 PostLog 上报便于调试。
std::string GetSelfInfoResponse();
