#pragma once

#include <vector>
#include <cstddef>

// ---------- SILK v3 解码器封装（基于 Skype SILK SDK，纯 C 编译） ----------

namespace silk_dec {

// 将 .silk 数据解码为 PCM（16-bit signed LE, 24000Hz, mono）
// silk_data: .silk 文件内容（含 SILK 头 #!SILK_V3 或 !SILK_V3）
// silk_len:  数据长度
// pcm_out:   输出 PCM 数据（追加到 vector）
// sample_rate: 输出采样率（通常 24000）
// 返回 true 成功
bool Decode(const void* silk_data, size_t silk_len,
            std::vector<unsigned char>& pcm_out,
            int& sample_rate);

} // namespace silk_dec
