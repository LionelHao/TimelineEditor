#include "CPUWatermarkFilter.h"
#include "LogUtil.h"
#include <cstring>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#define TAG "CPUWatermarkFilter"

CPUWatermarkFilter::CPUWatermarkFilter()
    : m_watermarkData(nullptr)
    , m_watermarkWidth(0)
    , m_watermarkHeight(0)
    , m_positionX(0)
    , m_positionY(0)
    , m_opacity(0.8f)
    , m_scale(0.2f)
{
}

CPUWatermarkFilter::~CPUWatermarkFilter() {
    if (m_watermarkData) {
        free(m_watermarkData);
        m_watermarkData = nullptr;
    }
}

int CPUWatermarkFilter::LoadWatermark(const char* imagePath) {
    if (!imagePath) {
        LOGCATE("%s: imagePath is null", TAG);
        return -1;
    }

    // 释放旧数据
    if (m_watermarkData) {
        free(m_watermarkData);
        m_watermarkData = nullptr;
    }

    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgba_frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws_ctx = nullptr;
    const AVCodec* codec = nullptr;
    int ret = -1;
    int video_stream_index = -1;
    int rgba_size = 0;
    uint8_t* rgba_buffer = nullptr;

    // 打开输入文件
    ret = avformat_open_input(&fmt_ctx, imagePath, nullptr, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avformat_open_input failed: %d", TAG, ret);
        return -1;
    }

    // 查找流信息
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avformat_find_stream_info failed: %d", TAG, ret);
        goto cleanup;
    }

    // 查找视频流
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        LOGCATE("%s: No video stream found", TAG);
        goto cleanup;
    }

    // 查找解码器
    codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        LOGCATE("%s: avcodec_find_decoder failed", TAG);
        goto cleanup;
    }

    // 分配解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        LOGCATE("%s: avcodec_alloc_context3 failed", TAG);
        goto cleanup;
    }

    // 复制解码器参数
    ret = avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    if (ret < 0) {
        LOGCATE("%s: avcodec_parameters_to_context failed: %d", TAG, ret);
        goto cleanup;
    }

    // 打开解码器
    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avcodec_open2 failed: %d", TAG, ret);
        goto cleanup;
    }

    // 分配帧
    frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !rgba_frame || !packet) {
        LOGCATE("%s: av_frame_alloc failed", TAG);
        goto cleanup;
    }

    // 读取帧
    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == 0) {
                // 转换为RGBA
                sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                         frame->width, frame->height, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!sws_ctx) {
                    LOGCATE("%s: sws_getContext failed", TAG);
                    goto cleanup;
                }

                // 分配RGBA缓冲区
                rgba_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame->width, frame->height, 1);
                rgba_buffer = (uint8_t*)av_malloc(rgba_size);
                if (!rgba_buffer) {
                    LOGCATE("%s: av_malloc failed", TAG);
                    goto cleanup;
                }

                av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgba_buffer,
                                    AV_PIX_FMT_RGBA, frame->width, frame->height, 1);

                sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
                         rgba_frame->data, rgba_frame->linesize);

                // 保存水印数据
                m_watermarkWidth = frame->width;
                m_watermarkHeight = frame->height;
                m_watermarkData = (uint8_t*)malloc(rgba_size);
                if (m_watermarkData) {
                    memcpy(m_watermarkData, rgba_buffer, rgba_size);
                    LOGCATE("%s: Loaded watermark %dx%d", TAG, m_watermarkWidth, m_watermarkHeight);
                    ret = 0;
                }

                av_free(rgba_buffer);
                rgba_buffer = nullptr;
                sws_freeContext(sws_ctx);
                sws_ctx = nullptr;
                break;
            }
        }
        av_packet_unref(packet);
    }

cleanup:
    if (rgba_buffer) av_free(rgba_buffer);
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (frame) av_frame_free(&frame);
    if (rgba_frame) av_frame_free(&rgba_frame);
    if (packet) av_packet_free(&packet);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);

    return ret;
}

void CPUWatermarkFilter::SetPosition(int x, int y) {
    m_positionX = x;
    m_positionY = y;
}

void CPUWatermarkFilter::SetOpacity(float opacity) {
    m_opacity = opacity;
}

void CPUWatermarkFilter::SetScale(float scale) {
    m_scale = scale;
}

// RGBA转YUV
static inline void RGBAToYUV(uint8_t r, uint8_t g, uint8_t b, uint8_t& y, uint8_t& u, uint8_t& v) {
    y = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
    u = (uint8_t)(-0.169f * r - 0.331f * g + 0.499f * b + 128);
    v = (uint8_t)(0.499f * r - 0.418f * g - 0.0813f * b + 128);
}

void CPUWatermarkFilter::BlendPixel(uint8_t* dstY, uint8_t* dstUV, int uvIndex,
                                    uint8_t srcR, uint8_t srcG, uint8_t srcB, uint8_t srcA,
                                    int x, int y, int width, int height) {
    // 计算混合因子
    float alpha = (srcA / 255.0f) * m_opacity;
    if (alpha <= 0.01f) return;

    // 目标YUV值
    uint8_t dstY_val = dstY[y * width + x];
    
    // 将源RGBA转换为YUV
    uint8_t srcY, srcU, srcV;
    RGBAToYUV(srcR, srcG, srcB, srcY, srcU, srcV);

    // 混合Y分量
    dstY[y * width + x] = (uint8_t)(srcY * alpha + dstY_val * (1.0f - alpha));

    // UV分量只在2x2块中存储一次，所以只在偶数坐标处理
    if ((x % 2 == 0) && (y % 2 == 0)) {
        // NV12格式: UV交错存储
        uint8_t dstU = dstUV[uvIndex];
        uint8_t dstV = dstUV[uvIndex + 1];

        // 混合UV分量
        dstUV[uvIndex] = (uint8_t)(srcU * alpha + dstU * (1.0f - alpha));
        dstUV[uvIndex + 1] = (uint8_t)(srcV * alpha + dstV * (1.0f - alpha));
    }
}

void CPUWatermarkFilter::ApplyToNV12(uint8_t* nv12Data, int width, int height) {
    if (!m_watermarkData || !nv12Data) {
        LOGCATE("%s: Cannot apply watermark - data=%p, nv12Data=%p", TAG, m_watermarkData, nv12Data);
        return;
    }

    LOGCATE("%s: Applying watermark to %dx%d, watermark=%dx%d, pos=(%d,%d), scale=%.2f, opacity=%.2f", 
            TAG, width, height, m_watermarkWidth, m_watermarkHeight, m_positionX, m_positionY, m_scale, m_opacity);

    // 计算缩放后的水印尺寸
    int scaledW = (int)(m_watermarkWidth * m_scale);
    int scaledH = (int)(m_watermarkHeight * m_scale);
    
    // 确保不超出视频边界
    if (m_positionX + scaledW > width) scaledW = width - m_positionX;
    if (m_positionY + scaledH > height) scaledH = height - m_positionY;
    if (scaledW <= 0 || scaledH <= 0) {
        LOGCATE("%s: Invalid watermark size: %dx%d", TAG, scaledW, scaledH);
        return;
    }

    LOGCATE("%s: Scaled watermark size: %dx%d", TAG, scaledW, scaledH);

    // Y平面起始位置
    uint8_t* yPlane = nv12Data;
    // UV平面起始位置 (Y平面之后)
    uint8_t* uvPlane = nv12Data + width * height;

    // 遍历水印像素
    int blendedPixels = 0;
    for (int wy = 0; wy < scaledH; wy++) {
        for (int wx = 0; wx < scaledW; wx++) {
            // 计算在源水印中的位置 (最近邻采样)
            int srcX = (wx * m_watermarkWidth) / scaledW;
            int srcY = (wy * m_watermarkHeight) / scaledH;
            
            // 获取源像素 (RGBA)
            int srcIdx = (srcY * m_watermarkWidth + srcX) * 4;
            uint8_t r = m_watermarkData[srcIdx];
            uint8_t g = m_watermarkData[srcIdx + 1];
            uint8_t b = m_watermarkData[srcIdx + 2];
            uint8_t a = m_watermarkData[srcIdx + 3];

            // 只处理有透明度的像素
            if (a > 0) {
                // 目标位置
                int dstX = m_positionX + wx;
                int dstY = m_positionY + wy;

                // 计算UV索引 (NV12格式)
                int uvY = dstY / 2;
                int uvX = dstX / 2;
                int uvIndex = uvY * width + uvX * 2;

                // 混合像素
                BlendPixel(yPlane, uvPlane, uvIndex, r, g, b, a, dstX, dstY, width, height);
                blendedPixels++;
            }
        }
    }

    LOGCATE("%s: Applied watermark at (%d,%d) size %dx%d, blended %d pixels", 
            TAG, m_positionX, m_positionY, scaledW, scaledH, blendedPixels);
}

void CPUWatermarkFilter::ApplyToRGBA(uint8_t* rgbaData, int width, int height) {
    if (!m_watermarkData || !rgbaData) {
        return;
    }

    // 计算缩放后的水印尺寸
    int scaledW = (int)(m_watermarkWidth * m_scale);
    int scaledH = (int)(m_watermarkHeight * m_scale);
    
    // 确保不超出视频边界
    if (m_positionX + scaledW > width) scaledW = width - m_positionX;
    if (m_positionY + scaledH > height) scaledH = height - m_positionY;
    if (scaledW <= 0 || scaledH <= 0) return;

    // 遍历水印像素
    for (int wy = 0; wy < scaledH; wy++) {
        for (int wx = 0; wx < scaledW; wx++) {
            // 计算在源水印中的位置
            int srcX = (wx * m_watermarkWidth) / scaledW;
            int srcY = (wy * m_watermarkHeight) / scaledH;
            
            // 获取源像素
            int srcIdx = (srcY * m_watermarkWidth + srcX) * 4;
            uint8_t srcA = m_watermarkData[srcIdx + 3];
            
            // 计算混合因子
            float alpha = (srcA / 255.0f) * m_opacity;
            if (alpha <= 0.01f) continue;

            // 目标位置
            int dstX = m_positionX + wx;
            int dstY = m_positionY + wy;
            int dstIdx = (dstY * width + dstX) * 4;

            // 混合RGBA
            for (int c = 0; c < 3; c++) { // R, G, B
                uint8_t src = m_watermarkData[srcIdx + c];
                uint8_t dst = rgbaData[dstIdx + c];
                rgbaData[dstIdx + c] = (uint8_t)(src * alpha + dst * (1.0f - alpha));
            }
            // Alpha通道保持255
            rgbaData[dstIdx + 3] = 255;
        }
    }
}
