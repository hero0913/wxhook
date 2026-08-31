# wxhook

> 基于学习目的开发的微信 Hook 研究项目。通过 DLL 注入 + Inline Hook 实现消息接收、图片解密、语音落盘、消息发送等功能，经 HTTP 接口与外部程序交互。

## 微信版本

`3.9.12.56`（32 位）这是32位的最后一版，一年多没更新了

> 不是很喜欢 4.0 版本的界面，本项目基于 3.9.12.56 开发。需要配置注册表绕过升级提醒,使用WxTools.exe启动微信，登录后注入 wxhook.dll。

### 绕过升级提醒

配置注册表，阻止微信检查更新：

```
计算机\HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
```

新建字符串值：

| 名称 | 数据 |
|------|------|
| `C:\Program Files (x86)\Tencent\WeChat\WeChat.exe` | `~ ARM64WOWONAMD64` |

设置后微信将跳过版本检测，不再弹出升级提醒。

## 使用方式

1. 将编译产出的 `wxhook.dll` 放到 WxTools.exe 同级目录下
2. 使用 WxTools.exe 启动微信
3. 微信登录成功后，在 WxTools.exe 中手动点击开始注入 `wxhook.dll`（使用其他注入工具也可以 原理都一样）
4. 启动接收端：`python tools/receiver.py`
5. 通过 HTTP 接口与微信交互

## 项目结构

```
wxhook/
├── src/
│   ├── core/
│   │   ├── config.h          # 全局配置（端口/版本/Hook 开关）
│   │   ├── dllmain.cpp       # DLL 入口，延迟启动 HTTP 服务 + Hook
│   │   ├── hooks.cpp         # Hook 统一入口（StartHooks/StopHooks）
│   │   └── pch.h / pch.cpp / framework.h
│   ├── http/
│   │   ├── http_server.cpp   # HTTP 服务（/status /login/state /self/info /send）
│   │   └── http_client.cpp   # HTTP 客户端（PostLog / PostJson，1 秒超时）
│   └── wx/
│       ├── wechat.cpp        # 微信基址获取、版本读取、编码转换
│       ├── self_info.cpp     # 获取登录状态与个人信息
│       ├── receive_msg.cpp   # Hook 接收消息，SEH 解析字段，异步转发
│       ├── image_decoder.cpp # 图片自动下载 Patch + XOR 解密 .dat→.png
│       ├── voice_msg.cpp     # Hook 语音数据，异步保存 .silk
│       ├── send_msg.cpp      # 发送文本消息（WeChatString + 内联汇编）
│       └── contact_list.cpp   # 获取好友/群聊列表（POD + SEH 保护）
├── third_party/
│   ├── httplib.h             # cpp-httplib 单头库
│   ├── json.hpp              # nlohmann/json 单头库
│   └── detours.h             # Detours 头（未用，备用）
├── tools/receiver.py         # Python 接收端（监听 9999，打印 log 与 message）
└── wxhook.sln / wxhook.vcxproj
```

## HTTP 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/status` | 微信版本校验（matched=true 表示版本正确） |
| GET | `/login/state` | 登录状态（logged=true/false） |
| GET | `/self/info` | 当前登录账号信息（未登录返回业务码 401） |
| POST | `/send` | 发送文本消息（body: `{"wxid":"...","content":"..."}`） |
| GET | `/contact/list` | 获取好友/群聊列表（群聊 wxcount/remark 为空） |
| GET | `/qr/url[?path=]` | 登录二维码 URL（带 path 额外生成 PNG 保存） |

## 消息流向

```
接收（微信 → DLL → 外部）:
  微信收到消息 → DLL Hook 回调
    ├─ SEH 解析 msgtype/wxid/sender/content/filepath
    ├─ CreateThread 异步线程（不阻塞微信）
    │   ├─ POST /message → receiver.py:9999
    │   └─ 图片: DecodeImageAsync 入队 → 工作线程 XOR 解密
    └─ 语音: Hook 原始数据 → 异步线程保存 .silk

发送（外部 → DLL → 微信）:
  POST /send {"wxid":"...","content":"..."}
    ├─ 登录预检 → 未登录返回 401
    ├─ UTF-8 → wchar_t* → WeChatString 结构
    └─ CriticalSection 加锁 → 内联汇编调用微信发送函数
```

## 配置

`config.h` 关键项：

| 配置 | 默认值 | 说明 |
|------|--------|------|
| `HTTP_PORT` | 8888 | HTTP 服务端口 |
| `EXPECTED_WX_VERSION` | 3.9.12.56 | 目标微信版本 |
| `LOG_PORT` / `MSG_PORT` | 9999 | receiver.py 监听端口 |
| `ENABLE_HOOKS` | true | Hook 总开关 |
| `ENABLE_IMAGE_DECODE` | true | 图片落盘解密 |
| `ENABLE_VOICE_SAVE` | true | 语音落盘保存 |
| `STARTUP_DELAY_MS` | 3000 | 注入后延迟启动（毫秒） |

## 编译

```bash
MSBuild wxhook.sln /p:Configuration=Debug /p:Platform=x86
```

产出 `Debug\wxhook.dll`，通过注入工具加载到 WeChat.exe。

## 接收端

```bash
python tools/receiver.py
```

监听 `0.0.0.0:9999`，收到 `POST /log` 和 `POST /message` 后终端打印，不落盘。

## 发送消息示例

```bash
# 发送文本消息（filehelper = 文件传输助手）
curl -X POST http://127.0.0.1:8888/send \
  -H "Content-Type: application/json" \
  -d '{"wxid":"filehelper","content":"测试消息"}'
```

## 获取好友列表示例

```bash
# 获取好友/群聊列表
curl http://127.0.0.1:8888/contact/list
```

返回示例：

```json
{
  "code": 0,
  "msg": "ok",
  "count": 2,
  "result": [
    {"wxid":"wxid_xxx", "nickname":"张三", "wxcount":"zhangsan", "remark":"老张"},
    {"wxid":"12345@chatroom", "nickname":"群名", "wxcount":"", "remark":""}
  ]
}
```

## 获取登录二维码示例

```bash
# 只获取二维码 URL（注入后立即可读，无 Hook 时序依赖）
curl http://127.0.0.1:8888/qr/url

# 获取 URL 并生成二维码 PNG 保存到指定路径
curl "http://127.0.0.1:8888/qr/url?path=C:\tmp\qr.png"
```

返回示例（带 path）：

```json
{
  "code": 200,
  "msg": "ok",
  "url": "http://weixin.qq.com/x/QdpCp1DjBQlloAAAAAAA",
  "suffix": "QdpCp1DjBQlloAAAAAAA",
  "saved_path": "C:\\tmp\\qr.png",
  "saved": true
}
```

## 后续开发计划

- 聊天记录数据库读取（解密 SQLite + MSG.db / MicroMsg.db）
- 联系人 / 群成员数据库查询
- 消息记录持久化存储

## 更新说明

### 2026-08-31

- 新增 `GET /qr/url` 接口，读取登录二维码 URL（直接读偏移，无 Hook）
- 支持 `?path=` 参数，服务端生成二维码 PNG 保存到指定路径
- 引入 qrcodegen + stb_image_write 单头库（二维码生成 + PNG 编码）

### 2026-08-28

- 新增 `GET /contact/list` 接口，获取好友/群聊列表
- 偏移引用统一改用 `src/wx/wx_offsets.h`

## 免责声明

本项目仅供学习交流与技术研究使用，不得用于任何违反法律法规或微信服务条款的场景。

如果本项目对你有帮助，欢迎给个 Star。

## 致谢

- [Ghidra](https://github.com/NationalSecurityAgency/ghidra) — NSA 开源逆向工程框架
- [x64dbg](https://github.com/x64dbg/x64dbg) — 开源 x86/x64 调试器
- [wechat-windows-versions](https://github.com/tom-snow/wechat-windows-versions) — 微信 Windows 历史版本归档
