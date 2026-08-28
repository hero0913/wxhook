#pragma once

// ============================================================
// wx_offsets.h — 本项目实际使用的微信 3.9.12.56 偏移地址
// 仅保留代码中引用的宏，避免引入无关版本偏移
// ============================================================

// ── 接收消息 Hook ──
#define WxReciveMessage       0x17b93b5    // 接收消息入口
#define WxReciveMessageCall   0x1199010    // 被覆盖的原始 call

// ── 个人信息 ──
#define WxID                  0x4368354    // 微信ID ASCII 指针基址
#define WxCount               WxID + 0x64   // 微信号
#define WxNickName            WxID + 0x10C  // 昵称
#define WxNation              WxID + 0xAC   // 国家
#define WxProvince            WxID + 0xC4   // 省份
#define WxCity                WxID + 0xDC   // 城市
#define WxPhoneNumber          WxID + 0x7C   // 手机号
#define WxCacheDir            WxID - 0x14   // 缓存目录 Unicode 指针
#define WxSex                 WxID + 0x1F0   // 性别

// ── 聊天记录字段偏移 ──
#define MsgTypeOffset         0x38         // 消息类型
#define IsPhoneMsg            0x3C         // 是否手机发送
#define MsgContentOffset      0x70         // 消息内容
#define MsgSourceOffset       0x1D8        // 消息来源
#define WxidOffset            0x48         // 微信ID/群ID
#define GroupMsgSenderOffset  0x17C        // 群消息发送者

// ── 各类消息文件路径偏移 ──
#define ImagePathOffset       0x1B8        // 图片路径
#define VideoPathOffset       0x1A4        // 视频路径
#define XmlImagePathOffset    0x1AC        // XML文章图片路径
#define FilePathOffset        0x1B8        // 文件路径

// ── 图片/视频自动下载补丁 ──
#define WxPatchAutoDownloadImage  0x166E9C2
#define WxPatchAutoDownloadVideo   0x166ed70

// ── 语音数据接收 Hook ──
#define WxReciveVoiceData     0x17478f1
#define WxReciveVoiceDataCall  0x2c77740
