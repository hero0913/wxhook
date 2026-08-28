#pragma once

// Hook 统一入口。
// 版本校验通过后由启动流程调用 StartHooks()。
// 实际 Detours 挂钩点（接收消息 / 发送消息等）由 03/04 功能点注册，
// 此处仅提供事务入口与占位实现。

// 开始 Hook（Detours 事务）。返回是否成功提交事务。
bool StartHooks();

// 卸载 Hook
void StopHooks();
