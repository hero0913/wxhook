#pragma once
#include <string>

// GET /contact/list -> 获取好友/群聊列表
// 返回 JSON:
//   未登录: {"code":401,"msg":"not logged in"}
//   成功  : {"code":0,"msg":"ok","count":N,"result":[{wxid,nickname,wxcount,remark}, ...]}
//   群聊  : wxcount/remark 为空字符串
//   失败  : {"code":500,"msg":"get contact list failed"}
std::string GetContactListResponse();
