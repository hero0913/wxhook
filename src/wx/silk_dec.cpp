#include "pch.h"
#include "silk_dec.h"
#include "SKP_Silk_SDK_API.h"   // SILK SDK 解码器 API（来自 third_party/silk_dec/interface）

#include <cstdlib>
#include <cstring>

namespace silk_dec {

namespace {
constexpr int kMaxBytesPerFrame = 1024;
constexpr int kMaxInputFrames   = 5;
constexpr int kFrameLengthMs     = 20;
constexpr int kMaxApiFsKHz       = 48;
constexpr int kDefaultSampleRate = 24000;
}

bool Decode(const void* silk_data, size_t silk_len,
            std::vector<unsigned char>& pcm_out,
            int& sample_rate)
{
    if (!silk_data || silk_len < 2) return false;

    const unsigned char* data = (const unsigned char*)silk_data;
    size_t pos = 0;

    // 跳过 SILK 头（#!SILK_V3 或 !SILK_V3）
    // 微信 .silk 变体：[0x02]#!SILK_V3[payload]，前面多 1 字节 type
    // 扫描前 16 字节定位标准头位置
    for (size_t i = 0; i + 9 <= silk_len && i < 16; ++i) {
        if (memcmp(data + i, "#!SILK_V3", 9) == 0) {
            pos = i + 9;
            break;
        }
        if (memcmp(data + i, "!SILK_V3", 8) == 0) {
            pos = i + 8;
            break;
        }
    }
    // 无标准头则从 0 开始（兼容无头变体）

    // 获取解码器状态大小
    SKP_int32 decSize = 0;
    if (SKP_Silk_SDK_Get_Decoder_Size(&decSize) != 0 || decSize <= 0)
        return false;

    void* psDec = malloc(decSize);
    if (!psDec) return false;

    // 初始化解码器
    if (SKP_Silk_SDK_InitDecoder(psDec) != 0) {
        free(psDec);
        return false;
    }

    // 解码控制参数
    SKP_SILK_SDK_DecControlStruct decControl;
    memset(&decControl, 0, sizeof(decControl));
    decControl.API_sampleRate  = kDefaultSampleRate;
    decControl.framesPerPacket = 1;
    sample_rate = kDefaultSampleRate;

    // 输出缓冲区（单帧最大 960 samples * 5 frames * 2 bytes）
    SKP_int16 outBuf[((kFrameLengthMs * kMaxApiFsKHz) << 1) * kMaxInputFrames];

    bool gotPcm = false;

    // 循环读取 payload 并解码
    while (pos + 2 <= silk_len) {
        // payload 长度（2 字节小端）
        SKP_int16 nBytes = (SKP_int16)(data[pos] | (data[pos + 1] << 8));
        pos += 2;

        if (nBytes <= 0 || pos + (size_t)nBytes > silk_len) break;

        const SKP_uint8* payload = (const SKP_uint8*)(data + pos);
        pos += nBytes;

        // 解码一个 packet 中的所有帧
        SKP_int16* outPtr = outBuf;
        SKP_int16 totLen  = 0;
        int frames = 0;

        do {
            SKP_int16 len = 0;
            int ret = SKP_Silk_SDK_Decode(psDec, &decControl, 0,
                                          payload, nBytes, outPtr, &len);
            if (ret != 0) break;  // 解码错误，跳过此 packet

            frames++;
            outPtr += len;
            totLen += len;
            if (frames > kMaxInputFrames) break;  // 防止损坏流
        } while (decControl.moreInternalDecoderFrames);

        if (totLen > 0) {
            pcm_out.insert(pcm_out.end(),
                          (unsigned char*)outBuf,
                          (unsigned char*)outBuf + (size_t)totLen * sizeof(SKP_int16));
            gotPcm = true;
        }
    }

    free(psDec);
    return gotPcm;
}

} // namespace silk_dec
