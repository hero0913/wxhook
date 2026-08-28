#pragma once

// 基于 cpp-httplib 的内置 HTTP 服务（对外提供功能接口）
// 启动后监听本地端口，注册路由；02/03/04 的接口后续在此注册。

// 启动 HTTP 服务（非阻塞：内部线程监听）。返回是否绑定成功。
bool StartHttpServer(int port);

// 停止 HTTP 服务
void StopHttpServer();

// 服务是否正在运行
bool IsHttpServerRunning();
