#include "WatermarkFilter.h"
#include "LogUtil.h"
#include "GLUtils.h"
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#define TAG "WatermarkFilter"

static const char* WATERMARK_VERTEX_SHADER = 
    "#version 300 es\n"
    "layout(location = 0) in vec4 a_position;\n"
    "layout(location = 1) in vec2 a_texCoord;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = a_position;\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

static const char* WATERMARK_FRAGMENT_SHADER = 
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_texCoord;\n"
    "layout(location = 0) out vec4 outColor;\n"
    "uniform sampler2D u_inputTexture;\n"
    "uniform sampler2D u_watermarkTexture;\n"
    "uniform vec4 u_watermarkRect;\n"
    "uniform float u_opacity;\n"
    "\n"
    "void main() {\n"
    "    vec4 videoColor = texture(u_inputTexture, v_texCoord);\n"
    "    \n"
    "    vec2 watermarkCoord = (v_texCoord - u_watermarkRect.xy) / u_watermarkRect.zw;\n"
    "    \n"
    "    if (watermarkCoord.x >= 0.0 && watermarkCoord.x <= 1.0 &&\n"
    "        watermarkCoord.y >= 0.0 && watermarkCoord.y <= 1.0) {\n"
    "        vec2 flippedCoord = vec2(watermarkCoord.x, 1.0 - watermarkCoord.y);\n"
    "        vec4 watermarkColor = texture(u_watermarkTexture, flippedCoord);\n"
    "        float alpha = watermarkColor.a * u_opacity;\n"
    "        outColor = mix(videoColor, watermarkColor, alpha);\n"
    "    } else {\n"
    "        outColor = videoColor;\n"
    "    }\n"
    "}\n";

WatermarkFilter::WatermarkFilter()
    : m_watermarkTexture(0)
    , m_hasWatermark(false)
    , m_positionLoc(-1)
    , m_texCoordLoc(-1)
    , m_inputTextureLoc(-1)
    , m_watermarkTextureLoc(-1)
    , m_watermarkRectLoc(-1)
    , m_opacityLoc(-1)
{
    memset(&m_watermarkImage, 0, sizeof(NativeImage));
}

WatermarkFilter::~WatermarkFilter() {
    // 直接清理资源，不调用虚函数
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }

    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    if (m_vbo[0] || m_vbo[1]) {
        glDeleteBuffers(2, m_vbo);
        memset(m_vbo, 0, sizeof(m_vbo));
    }

    if (m_watermarkTexture) {
        glDeleteTextures(1, &m_watermarkTexture);
        m_watermarkTexture = 0;
    }

    if (m_watermarkImage.ppPlane[0]) {
        free(m_watermarkImage.ppPlane[0]);
        m_watermarkImage.ppPlane[0] = nullptr;
    }

    m_initialized = false;
}

int WatermarkFilter::Init() {
    if (m_initialized) {
        return 0;
    }

    m_program = CreateProgram(WATERMARK_VERTEX_SHADER, WATERMARK_FRAGMENT_SHADER);
    if (m_program == 0) {
        LOGCATE("%s: CreateProgram failed", TAG);
        return -1;
    }

    m_positionLoc = glGetAttribLocation(m_program, "a_position");
    m_texCoordLoc = glGetAttribLocation(m_program, "a_texCoord");
    m_inputTextureLoc = glGetUniformLocation(m_program, "u_inputTexture");
    m_watermarkTextureLoc = glGetUniformLocation(m_program, "u_watermarkTexture");
    m_watermarkRectLoc = glGetUniformLocation(m_program, "u_watermarkRect");
    m_opacityLoc = glGetUniformLocation(m_program, "u_opacity");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(2, m_vbo);

    glBindVertexArray(m_vao);

    static const GLfloat vertexCoords[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    static const GLfloat texCoords[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexCoords), vertexCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_positionLoc);
    glVertexAttribPointer(m_positionLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texCoords), texCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_texCoordLoc);
    glVertexAttribPointer(m_texCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindVertexArray(0);

    m_initialized = true;
    LOGCATE("%s: WatermarkFilter initialized", TAG);

    return 0;
}

void WatermarkFilter::UnInit() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }

    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    if (m_vbo[0] || m_vbo[1]) {
        glDeleteBuffers(2, m_vbo);
        memset(m_vbo, 0, sizeof(m_vbo));
    }

    if (m_watermarkTexture) {
        glDeleteTextures(1, &m_watermarkTexture);
        m_watermarkTexture = 0;
    }

    if (m_watermarkImage.ppPlane[0]) {
        free(m_watermarkImage.ppPlane[0]);
        m_watermarkImage.ppPlane[0] = nullptr;
    }

    m_initialized = false;
    m_hasWatermark = false;
}

int WatermarkFilter::LoadImageWithFFmpeg(const char* imagePath, NativeImage* image) {
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

    // Open input file
    ret = avformat_open_input(&fmt_ctx, imagePath, nullptr, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avformat_open_input failed: %d", TAG, ret);
        return -1;
    }

    // Find stream info
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avformat_find_stream_info failed: %d", TAG, ret);
        goto cleanup;
    }

    // Find video stream
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

    // Find decoder
    codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        LOGCATE("%s: avcodec_find_decoder failed", TAG);
        goto cleanup;
    }

    // Allocate codec context
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        LOGCATE("%s: avcodec_alloc_context3 failed", TAG);
        goto cleanup;
    }

    // Copy codec parameters
    ret = avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    if (ret < 0) {
        LOGCATE("%s: avcodec_parameters_to_context failed: %d", TAG, ret);
        goto cleanup;
    }

    // Open codec
    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avcodec_open2 failed: %d", TAG, ret);
        goto cleanup;
    }

    // Allocate frames
    frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !rgba_frame || !packet) {
        LOGCATE("%s: av_frame_alloc failed", TAG);
        goto cleanup;
    }

    // Read frame
    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0) {
                LOGCATE("%s: avcodec_send_packet failed: %d", TAG, ret);
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == 0) {
                // Convert to RGBA
                sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                         frame->width, frame->height, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!sws_ctx) {
                    LOGCATE("%s: sws_getContext failed", TAG);
                    goto cleanup;
                }

                // Allocate RGBA buffer
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

                // Copy to NativeImage
                image->width = frame->width;
                image->height = frame->height;
                image->format = IMAGE_FORMAT_RGBA;
                NativeImageUtil::AllocNativeImage(image);
                memcpy(image->ppPlane[0], rgba_buffer, rgba_size);

                av_free(rgba_buffer);
                rgba_buffer = nullptr;
                sws_freeContext(sws_ctx);
                sws_ctx = nullptr;

                LOGCATE("%s: Loaded image %dx%d", TAG, image->width, image->height);
                ret = 0;
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

int WatermarkFilter::SetWatermarkImage(const char* imagePath) {
    if (imagePath == nullptr) {
        LOGCATE("%s: imagePath is null", TAG);
        return -1;
    }

    m_config.imagePath = imagePath;
    
    // Load image from file
    if (m_watermarkImage.ppPlane[0]) {
        free(m_watermarkImage.ppPlane[0]);
        m_watermarkImage.ppPlane[0] = nullptr;
    }
    
    LOGCATE("%s: Loading watermark from: %s", TAG, imagePath);
    
    // Load image using FFmpeg
    int result = LoadImageWithFFmpeg(imagePath, &m_watermarkImage);
    if (result != 0) {
        LOGCATE("%s: Failed to load image from: %s", TAG, imagePath);
        return -1;
    }
    
    m_config.width = m_watermarkImage.width;
    m_config.height = m_watermarkImage.height;
    m_hasWatermark = true;
    
    LOGCATE("%s: Loaded watermark image %dx%d", TAG, m_config.width, m_config.height);
    
    return CreateWatermarkTexture();
}

int WatermarkFilter::SetWatermarkImage(NativeImage* image) {
    if (image == nullptr || image->ppPlane[0] == nullptr) {
        LOGCATE("%s: Invalid image", TAG);
        return -1;
    }

    if (m_watermarkImage.ppPlane[0]) {
        free(m_watermarkImage.ppPlane[0]);
    }

    m_watermarkImage.width = image->width;
    m_watermarkImage.height = image->height;
    m_watermarkImage.format = image->format;
    NativeImageUtil::AllocNativeImage(&m_watermarkImage);
    memcpy(m_watermarkImage.ppPlane[0], image->ppPlane[0], 
           image->width * image->height * 4);

    m_config.width = image->width;
    m_config.height = image->height;
    m_hasWatermark = true;

    return CreateWatermarkTexture();
}

int WatermarkFilter::CreateWatermarkTexture() {
    if (m_watermarkTexture == 0) {
        glGenTextures(1, &m_watermarkTexture);
    }

    glBindTexture(GL_TEXTURE_2D, m_watermarkTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (m_watermarkImage.ppPlane[0] && m_watermarkImage.width > 0 && m_watermarkImage.height > 0) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
                     m_watermarkImage.width, m_watermarkImage.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, m_watermarkImage.ppPlane[0]);
        m_hasWatermark = true;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    return 0;
}

void WatermarkFilter::SetPosition(WatermarkPosition position) {
    m_config.position = position;
}

void WatermarkFilter::SetOpacity(float opacity) {
    m_config.opacity = opacity;
}

void WatermarkFilter::SetScale(float scale) {
    m_config.scale = scale;
}

void WatermarkFilter::SetOffset(float offsetX, float offsetY) {
    m_config.offsetX = offsetX;
    m_config.offsetY = offsetY;
}

void WatermarkFilter::CalculatePosition(int videoWidth, int videoHeight, 
                                        float& x, float& y, float& w, float& h) {
    float aspectRatio = (float)m_config.width / m_config.height;
    float videoAspect = (float)videoWidth / videoHeight;
    
    if (aspectRatio > videoAspect) {
        w = m_config.scale;
        h = w / aspectRatio * videoAspect;
    } else {
        h = m_config.scale;
        w = h * aspectRatio / videoAspect;
    }

    switch (m_config.position) {
        case WatermarkPosition::POSITION_TOP_LEFT:
            x = m_config.offsetX;
            y = 1.0f - h - m_config.offsetY;
            break;
        case WatermarkPosition::POSITION_TOP_RIGHT:
            x = 1.0f - w - m_config.offsetX;
            y = 1.0f - h - m_config.offsetY;
            break;
        case WatermarkPosition::POSITION_BOTTOM_LEFT:
            x = m_config.offsetX;
            y = m_config.offsetY;
            break;
        case WatermarkPosition::POSITION_BOTTOM_RIGHT:
            x = 1.0f - w - m_config.offsetX;
            y = m_config.offsetY;
            break;
        case WatermarkPosition::POSITION_CENTER:
            x = (1.0f - w) / 2.0f;
            y = (1.0f - h) / 2.0f;
            break;
        case WatermarkPosition::POSITION_CUSTOM:
            x = m_config.offsetX;
            y = m_config.offsetY;
            break;
        default:
            x = 1.0f - w - m_config.offsetX;
            y = m_config.offsetY;
            break;
    }
}

void WatermarkFilter::Apply(GLuint inputTexture, GLuint outputFBO, int width, int height) {
    if (!m_initialized) {
        LOGCATE("%s: Filter not initialized", TAG);
        return;
    }
    
    if (!m_hasWatermark) {
        LOGCATE("%s: No watermark set", TAG);
        return;
    }
    
    if (m_watermarkTexture == 0) {
        LOGCATE("%s: Watermark texture not created", TAG);
        return;
    }
    
    LOGCATE("%s: Applying watermark, video=%dx%d, watermark=%dx%d", 
            TAG, width, height, m_config.width, m_config.height);

    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, width, height);

    glUseProgram(m_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(m_inputTextureLoc, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_watermarkTexture);
    glUniform1i(m_watermarkTextureLoc, 1);

    float x, y, w, h;
    CalculatePosition(width, height, x, y, w, h);
    LOGCATE("%s: Watermark position calculated: pos=%d, x=%f, y=%f, w=%f, h=%f, offsetX=%f, offsetY=%f", 
            TAG, (int)m_config.position, x, y, w, h, m_config.offsetX, m_config.offsetY);
    glUniform4f(m_watermarkRectLoc, x, y, w, h);
    glUniform1f(m_opacityLoc, m_config.opacity);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
