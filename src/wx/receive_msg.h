#pragma once

// 03 接收消息 Hook：
// - inline hook 覆盖 WxReciveMessage（微信基址+0x17b93b5）的入口 5 字节，
//   改为跳转到我们的 __declspec(naked) 跳板；
// - 跳板保存现场 -> 取 ECX 作为 msg 结构指针 -> 交给 C++ 解析层 ->
//   调用被覆盖的原始 call（WxReciveMessageCall @ base+0x1199010）-> 跳到返回地址；
// - C++ 解析层用 SEH 保护，读取 MsgType/IsPhoneMsg/MsgContent/Wxid/Sender/
//   Source/FilePath 字段，PostLog 调试后异步 POST /message 转发。

// 安装 Hook（StartHooks 会调用，外部不用直接调）。
bool InstallReceiveMessageHook();

// 卸载 Hook（StopHooks 会调用）。
void UninstallReceiveMessageHook();
