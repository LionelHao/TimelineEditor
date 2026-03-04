#include "TimelineGLRender.h"
#include "GLUtils.h"
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define TAG "TimelineGLRender"

// RGBA 简单着色器
static const char* RGBA_VERTEX_SHADER = 
    "#version 300 es\n"
    "layout(location = 0) in vec4 a_position;\n"
    "layout(location = 1) in vec2 a_texCoord;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = a_position;\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

static const char* RGBA_FRAGMENT_SHADER = 
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_texCoord;\n"
    "layout(location = 0) out vec4 outColor;\n"
    "uniform sampler2D s_texture;\n"
    "\n"
    "void main() {\n"
    "    outColor = texture(s_texture, v_texCoord);\n"
    "}\n";

// YUV转RGB着色器（NV12格式）
static const char* YUV_VERTEX_SHADER = 
    "#version 300 es\n"
    "layout(location = 0) in vec4 a_position;\n"
    "layout(location = 1) in vec2 a_texCoord;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = a_position;\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

static const char* YUV_FRAGMENT_SHADER = 
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_texCoord;\n"
    "layout(location = 0) out vec4 outColor;\n"
    "uniform sampler2D s_yTexture;\n"
    "uniform sampler2D s_uvTexture;\n"
    "\n"
    "void main() {\n"
    "    float y = texture(s_yTexture, v_texCoord).r;\n"
    "    vec2 uv = texture(s_uvTexture, v_texCoord).rg - 0.5;\n"
    "    float r = y + 1.402 * uv.y;\n"
    "    float g = y - 0.344 * uv.x - 0.714 * uv.y;\n"
    "    float b = y + 1.772 * uv.x;\n"
    "    outColor = vec4(r, g, b, 1.0);\n"
    "}\n";

// 顶点坐标
static const GLfloat verticesCoords[] = {
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
};

// 纹理坐标
static const GLfloat textureCoords[] = {
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
};

// 索引
static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

TimelineGLRender::TimelineGLRender()
    : m_watermarkFilter(nullptr)
    , m_width(0)
    , m_height(0)
    , m_initialized(false)
    , m_rgbaProgram(0)
    , m_rgbaPosLoc(-1)
    , m_rgbaTexCoordLoc(-1)
    , m_rgbaTextureLoc(-1)
    , m_watermarkTexture(0)
    , m_hasWatermark(false)
    , m_blurProgramH(0)
    , m_blurProgramV(0)
    , m_blurVAO(0)
    , m_blurPosLoc(-1)
    , m_blurTexCoordLoc(-1)
    , m_blurTextureLoc(-1)
    , m_blurRadiusLoc(-1)
    , m_yuvProgram(0)
    , m_yuvVAO(0)
    , m_yuvPosLoc(-1)
    , m_yuvTexCoordLoc(-1)
    , m_yuvYTexLoc(-1)
    , m_yuvUVTexLoc(-1)
    , m_yTexture(0)
    , m_uvTexture(0)
    , m_fbo1(0), m_fbo2(0), m_fbo3(0)
    , m_texture1(0), m_texture2(0), m_texture3(0)
    , m_yBuffer(nullptr)
    , m_uvBuffer(nullptr)
    , m_yBufferSize(0)
    , m_uvBufferSize(0)
{
}

TimelineGLRender::~TimelineGLRender() {
    // 直接调用Uninit，它是幂等的
    Uninit();
}

int TimelineGLRender::Init() {
    if (m_initialized) {
        LOGCATE("%s: Already initialized", TAG);
        return 0;
    }

    m_width = 1920;
    m_height = 1080;

    if (m_eglContext.Init(m_width, m_height) != 0) {
        LOGCATE("%s: EGLContext init failed", TAG);
        return -1;
    }

    if (InitRGBARenderer() != 0) {
        LOGCATE("%s: InitRGBARenderer failed", TAG);
        m_eglContext.Uninit();
        return -1;
    }

    if (InitYUVRenderer() != 0) {
        LOGCATE("%s: InitYUVRenderer failed", TAG);
        m_eglContext.Uninit();
        return -1;
    }

    m_watermarkFilter = new WatermarkFilter();
    if (m_watermarkFilter) {
        m_watermarkFilter->Init();
    }

    if (InitBlurRenderer() != 0) {
        LOGCATE("%s: InitBlurRenderer failed", TAG);
    }

    if (InitReusableResources() != 0) {
        LOGCATE("%s: InitReusableResources failed", TAG);
    }

    m_initialized = true;
    LOGCATE("%s: Initialized", TAG);
    return 0;
}

int TimelineGLRender::InitWithEGLContext(EGLContext eglContext, EGLDisplay eglDisplay) {
    if (m_initialized) {
        LOGCATE("%s: Already initialized", TAG);
        return 0;
    }

    m_width = 1920;
    m_height = 1080;

    if (m_eglContext.InitWithExternalContext(eglContext, eglDisplay) != 0) {
        LOGCATE("%s: EGLContext init with external context failed", TAG);
        return -1;
    }

    if (InitRGBARenderer() != 0) {
        LOGCATE("%s: InitRGBARenderer failed", TAG);
        m_eglContext.Uninit();
        return -1;
    }

    if (InitYUVRenderer() != 0) {
        LOGCATE("%s: InitYUVRenderer failed", TAG);
        m_eglContext.Uninit();
        return -1;
    }

    m_watermarkFilter = new WatermarkFilter();
    if (m_watermarkFilter) {
        m_watermarkFilter->Init();
    }

    if (InitBlurRenderer() != 0) {
        LOGCATE("%s: InitBlurRenderer failed", TAG);
    }

    if (InitReusableResources() != 0) {
        LOGCATE("%s: InitReusableResources failed", TAG);
    }

    m_initialized = true;
    LOGCATE("%s: Initialized with external EGL context", TAG);
    return 0;
}

void TimelineGLRender::SetSurfaceSize(int width, int height) {
    if (!m_initialized) {
        return;
    }
    m_width = width;
    m_height = height;
    LOGCATE("%s: Set surface size to %dx%d", TAG, width, height);
}

int TimelineGLRender::RenderFrameToWindow(AVFrame* avFrame) {
    LOGCATE("%s: RenderFrameToWindow called, m_initialized=%d, avFrame=%p", TAG, m_initialized, avFrame);
    if (!m_initialized || !avFrame) {
        LOGCATE("%s: RenderFrameToWindow - conditions not met", TAG);
        return -1;
    }

    if (m_eglContext.GetSurface() != EGL_NO_SURFACE) {
        if (!m_eglContext.MakeCurrent()) {
            LOGCATE("%s: Failed to make EGL context current", TAG);
            return -1;
        }
    }
    LOGCATE("%s: EGL context made current, surface=%p", TAG, m_eglContext.GetSurface());

    glClear(GL_COLOR_BUFFER_BIT);

    if (avFrame->format == AV_PIX_FMT_RGBA) {
        GLuint texture = 0;
        UploadRGBAToTexture(avFrame->data[0], avFrame->width, avFrame->height, &texture);

        glUseProgram(m_rgbaProgram);
        glBindVertexArray(m_rgbaVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(m_rgbaTextureLoc, 0);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        glDeleteTextures(1, &texture);
    } else if (avFrame->format == AV_PIX_FMT_YUV420P || avFrame->format == AV_PIX_FMT_YUVJ420P) {
        UploadYUVToTextures(avFrame);

        glUseProgram(m_yuvProgram);
        glBindVertexArray(m_yuvVAO);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_yTexture);
        glUniform1i(m_yuvYTexLoc, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_uvTexture);
        glUniform1i(m_yuvUVTexLoc, 1);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    }

    EGLSurface surface = m_eglContext.GetSurface();
    LOGCATE("%s: RenderFrameToWindow - surface=%p", TAG, surface);
    if (surface != EGL_NO_SURFACE) {
        m_eglContext.SwapBuffers();
    }
    return 0;
}

void TimelineGLRender::Uninit() {
    if (!m_initialized) {
        return;
    }

    // 销毁复用资源
    UninitReusableResources();

    // 销毁水印滤镜 - 析构函数会自己清理资源
    if (m_watermarkFilter) {
        delete m_watermarkFilter;
        m_watermarkFilter = nullptr;
    }

    // 销毁 YUV 渲染器
    UninitYUVRenderer();

    // 销毁 RGBA 渲染器
    UninitRGBARenderer();
    
    // 销毁模糊渲染器
    UninitBlurRenderer();

    // 销毁 EGL 上下文
    m_eglContext.Uninit();

    m_initialized = false;
    LOGCATE("%s: Uninitialized", TAG);
}

int TimelineGLRender::SetWatermark(const char* imagePath, float opacity, float scale, float offsetX, float offsetY) {
    LOGCATE("%s: SetWatermark called (path=%s, watermarkFilter=%p)", TAG, 
            imagePath ? imagePath : "(null)", m_watermarkFilter);
    
    if (!m_watermarkFilter) {
        LOGCATE("%s: WatermarkFilter is null", TAG);
        return -1;
    }
    m_hasWatermark = true;
    
    // 设置水印图像
    if (m_watermarkFilter->SetWatermarkImage(imagePath) != 0) {
        LOGCATE("%s: SetWatermarkImage failed", TAG);
        return -1;
    }
    
    // 设置其他参数
    m_watermarkFilter->SetOpacity(opacity);
    m_watermarkFilter->SetScale(scale);
    m_watermarkFilter->SetOffset(offsetX, offsetY);
    
    LOGCATE("%s: Watermark set successfully (path=%s, opacity=%f, scale=%f, offset=(%f,%f), hasWatermark=%d)", 
            TAG, imagePath, opacity, scale, offsetX, offsetY, m_hasWatermark);
    return 0;
}

int TimelineGLRender::RenderFrame(AVFrame* avFrame, uint8_t** outNV12Data, int* outNV12Size) {
    if (!m_initialized || !avFrame) {
        return -1;
    }

    if (!m_eglContext.MakeCurrent()) {
        LOGCATE("%s: MakeCurrent failed", TAG);
        return -1;
    }

    glGetError();

    int srcW = avFrame->width;
    int srcH = avFrame->height;
    int canvasW = m_width;
    int canvasH = m_height;
    
    float srcAspect = (float)srcW / srcH;
    float canvasAspect = (float)canvasW / canvasH;
    
    int dstW, dstH;
    int offsetX, offsetY;
    
    if (srcAspect > canvasAspect) {
        dstW = canvasW;
        dstH = (int)(canvasW / srcAspect);
        if (dstH > canvasH) dstH = canvasH;
    } else if (srcAspect < canvasAspect) {
        dstH = canvasH;
        dstW = (int)(canvasH * srcAspect);
        if (dstW > canvasW) dstW = canvasW;
    } else {
        dstW = canvasW;
        dstH = canvasH;
    }
    
    offsetX = (canvasW - dstW) / 2;
    offsetY = (canvasH - dstH) / 2;

    // 检查是否支持GPU YUV转换
    // YUV420P, YUVJ420P (JPEG色彩空间的YUV420P), NV12等格式
    AVPixelFormat pixFormat = (AVPixelFormat)avFrame->format;
    bool useGpuYuv = (pixFormat == AV_PIX_FMT_YUV420P || 
                      pixFormat == AV_PIX_FMT_YUVJ420P ||
                      pixFormat == AV_PIX_FMT_NV12);
    
    LOGCATE("%s: Frame format=%d, useGpuYuv=%d, size=%dx%d, canvas=%dx%d", 
            TAG, avFrame->format, useGpuYuv, srcW, srcH, canvasW, canvasH);

    // 1. 渲染背景（拉伸填满画布）
    if (useGpuYuv) {
        // GPU YUV转换
        UploadYUVToTextures(avFrame);
        RenderYUVToFBO(m_fbo1, canvasW, canvasH, 0, 0);
    } else {
        // CPU sws_scale转换
        int bgRgbaSize = canvasW * canvasH * 4;
        uint8_t* bgRgbaBuffer = (uint8_t*)malloc(bgRgbaSize);
        if (!bgRgbaBuffer) {
            m_eglContext.DoneCurrent();
            return -1;
        }

        SwsContext* bgSwsCtx = sws_getContext(
            srcW, srcH, (AVPixelFormat)avFrame->format,
            canvasW, canvasH, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!bgSwsCtx) {
            free(bgRgbaBuffer);
            m_eglContext.DoneCurrent();
            return -1;
        }

        uint8_t* bgDstData[4] = { bgRgbaBuffer, nullptr, nullptr, nullptr };
        int bgDstLinesize[4] = { canvasW * 4, 0, 0, 0 };

        sws_scale(bgSwsCtx, avFrame->data, avFrame->linesize, 0, srcH, bgDstData, bgDstLinesize);
        sws_freeContext(bgSwsCtx);

        GLuint bgTexture = 0;
        UploadRGBAToTexture(bgRgbaBuffer, canvasW, canvasH, &bgTexture);
        free(bgRgbaBuffer);

        RenderStretchToFBO(bgTexture, m_fbo1);
        glDeleteTextures(1, &bgTexture);
    }

    // 2. 对FBO1的纹理进行高斯模糊，输出到FBO2
    ApplyGaussianBlur(m_texture1, m_fbo2, canvasW, canvasH, 25.0f);

    // 3. 将模糊背景复制到FBO3
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo3);
    glViewport(0, 0, canvasW, canvasH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_rgbaProgram);
    glBindVertexArray(m_rgbaVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture2);
    glUniform1i(m_rgbaTextureLoc, 0);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 4. 将前景渲染到FBO3（保持宽高比，居中显示，需要混合）
    if (useGpuYuv) {
        // GPU YUV转换，渲染前景
        UploadYUVToTextures(avFrame);
        RenderYUVToFBOWithBlend(m_fbo3, dstW, dstH, offsetX, offsetY);
    } else {
        // CPU sws_scale转换前景
        int fgRgbaSize = dstW * dstH * 4;
        uint8_t* fgRgbaBuffer = (uint8_t*)malloc(fgRgbaSize);
        if (fgRgbaBuffer) {
            SwsContext* fgSwsCtx = sws_getContext(
                srcW, srcH, (AVPixelFormat)avFrame->format,
                dstW, dstH, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

            if (fgSwsCtx) {
                uint8_t* fgDstData[4] = { fgRgbaBuffer, nullptr, nullptr, nullptr };
                int fgDstLinesize[4] = { dstW * 4, 0, 0, 0 };

                sws_scale(fgSwsCtx, avFrame->data, avFrame->linesize, 0, srcH, fgDstData, fgDstLinesize);
                sws_freeContext(fgSwsCtx);

                GLuint fgTexture = 0;
                UploadRGBAToTexture(fgRgbaBuffer, dstW, dstH, &fgTexture);
                RenderRGBAToFBOWithOffset(fgTexture, m_fbo3, dstW, dstH, offsetX, offsetY);
                glDeleteTextures(1, &fgTexture);
            }
            free(fgRgbaBuffer);
        }
    }

    // 5. 应用水印（如果有）
    GLuint finalFBO = m_fbo3;
    GLuint finalTexture = m_texture3;
    if (m_hasWatermark && m_watermarkFilter) {
        m_watermarkFilter->Apply(m_texture3, m_fbo1, canvasW, canvasH);
        finalFBO = m_fbo1;
        finalTexture = m_texture1;
    }

    // 6. 读回NV12数据
    if (ReadBackNV12(finalFBO, canvasW, canvasH, outNV12Data, outNV12Size) != 0) {
        LOGCATE("%s: ReadBackNV12 failed", TAG);
        m_eglContext.DoneCurrent();
        return -1;
    }

    m_eglContext.DoneCurrent();
    return 0;
}

int TimelineGLRender::InitRGBARenderer() {
    // 创建着色器程序
    m_rgbaProgram = GLUtils::CreateProgram(RGBA_VERTEX_SHADER, RGBA_FRAGMENT_SHADER);
    if (m_rgbaProgram == 0) {
        LOGCATE("%s: Create RGBA program failed", TAG);
        return -1;
    }
    LOGCATE("%s: RGBA program created: %d", TAG, m_rgbaProgram);

    // 获取位置
    m_rgbaPosLoc = glGetAttribLocation(m_rgbaProgram, "a_position");
    m_rgbaTexCoordLoc = glGetAttribLocation(m_rgbaProgram, "a_texCoord");
    m_rgbaTextureLoc = glGetUniformLocation(m_rgbaProgram, "s_texture");

    // 创建 VAO、VBO 和 EBO
    glGenVertexArrays(1, &m_rgbaVAO);
    glGenBuffers(2, m_rgbaVBO);
    glGenBuffers(1, &m_rgbaEBO);

    glBindVertexArray(m_rgbaVAO);

    // 顶点缓冲
    glBindBuffer(GL_ARRAY_BUFFER, m_rgbaVBO[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verticesCoords), verticesCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_rgbaPosLoc);
    glVertexAttribPointer(m_rgbaPosLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    // 纹理坐标缓冲
    glBindBuffer(GL_ARRAY_BUFFER, m_rgbaVBO[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoords), textureCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_rgbaTexCoordLoc);
    glVertexAttribPointer(m_rgbaTexCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    // 索引缓冲
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_rgbaEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    LOGCATE("%s: RGBA renderer initialized", TAG);
    return 0;
}

void TimelineGLRender::UninitRGBARenderer() {
    if (m_rgbaProgram) {
        glDeleteProgram(m_rgbaProgram);
        m_rgbaProgram = 0;
    }

    if (m_rgbaVAO) {
        glDeleteVertexArrays(1, &m_rgbaVAO);
        m_rgbaVAO = 0;
    }

    if (m_rgbaVBO[0] || m_rgbaVBO[1]) {
        glDeleteBuffers(2, m_rgbaVBO);
        memset(m_rgbaVBO, 0, sizeof(m_rgbaVBO));
    }

    if (m_rgbaEBO) {
        glDeleteBuffers(1, &m_rgbaEBO);
        m_rgbaEBO = 0;
    }
}

GLuint TimelineGLRender::CreateTexture() {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

GLuint TimelineGLRender::CreateFBO(int width, int height) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // 创建颜色附件纹理
    GLuint colorTexture;
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    // 检查 FBO 完整性
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGCATE("%s: FBO incomplete", TAG);
        glDeleteTextures(1, &colorTexture);
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

int TimelineGLRender::UploadRGBAToTexture(uint8_t* rgbaData, int width, int height, GLuint* outTexture) {
    GLuint texture = CreateTexture();
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGCATE("%s: glTexImage2D for RGBA error: %x", TAG, err);
        glDeleteTextures(1, &texture);
        return -1;
    }

    *outTexture = texture;
    return 0;
}

int TimelineGLRender::RenderRGBAToFBO(GLuint rgbaTexture, GLuint outputFBO) {
    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_rgbaProgram);
    glBindVertexArray(m_rgbaVAO);

    // 绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rgbaTexture);
    glUniform1i(m_rgbaTextureLoc, 0);

    // 绘制
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return 0;
}

int TimelineGLRender::RenderRGBAToFBOWithOffset(GLuint rgbaTexture, GLuint outputFBO, int textureW, int textureH, int offsetX, int offsetY) {
    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, m_width, m_height);
    
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float left = (2.0f * offsetX / m_width) - 1.0f;
    float right = (2.0f * (offsetX + textureW) / m_width) - 1.0f;
    float bottom = (2.0f * offsetY / m_height) - 1.0f;
    float top = (2.0f * (offsetY + textureH) / m_height) - 1.0f;

    LOGCATE("%s: RenderWithOffset: texture=%dx%d, offset=(%d,%d), NDC=[%.2f,%.2f,%.2f,%.2f]", 
            TAG, textureW, textureH, offsetX, offsetY, left, right, bottom, top);

    GLfloat vertices[] = {
        left,  top,
        left,  bottom,
        right, bottom,
        right, top,
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_rgbaVBO[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glUseProgram(m_rgbaProgram);
    glBindVertexArray(m_rgbaVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rgbaTexture);
    glUniform1i(m_rgbaTextureLoc, 0);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
    
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    static const GLfloat originalVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_rgbaVBO[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(originalVertices), originalVertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
}

int TimelineGLRender::ReadBackNV12(GLuint outputFBO, int width, int height, uint8_t** outNV12Data, int* outNV12Size) {
    // 分配 NV12 缓冲区（每次都分配新的，让调用者 free）
    int nv12Size = width * height * 3 / 2;
    uint8_t* nv12Data = (uint8_t*)malloc(nv12Size);
    if (!nv12Data) {
        LOGCATE("%s: malloc failed for NV12 data", TAG);
        return -1;
    }

    // 绑定 FBO
    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);

    // 设置像素对齐
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // 读取 RGBA 数据
    uint8_t* rgbaBuffer = (uint8_t*)malloc(width * height * 4);
    if (!rgbaBuffer) {
        LOGCATE("%s: malloc failed for RGBA buffer", TAG);
        free(nv12Data);
        return -1;
    }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBuffer);

    // 检查 OpenGL 错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGCATE("%s: glReadPixels error: %x", TAG, err);
    }

    // 将 RGBA 转换为 NV12（注意：OpenGL 的图像是颠倒的）
    uint8_t* yPlane = nv12Data;
    uint8_t* uvPlane = nv12Data + width * height;

    for (int y = 0; y < height; y++) {
        int glY = height - 1 - y;  // 翻转 Y 坐标
        uint8_t* rgba = rgbaBuffer + glY * width * 4;
        uint8_t* yOut = yPlane + y * width;

        for (int x = 0; x < width; x++) {
            uint8_t r = rgba[x * 4 + 0];
            uint8_t g = rgba[x * 4 + 1];
            uint8_t b = rgba[x * 4 + 2];

            // RGB to Y (BT.601)
            yOut[x] = (uint8_t)(0.257f * r + 0.504f * g + 0.098f * b + 16.0f);
        }
    }

    // 下采样 UV（也需要翻转）
    for (int y = 0; y < height / 2; y++) {
        int glY0 = height - 1 - (y * 2);
        int glY1 = height - 1 - (y * 2 + 1);
        uint8_t* rgba0 = rgbaBuffer + glY0 * width * 4;
        uint8_t* rgba1 = rgbaBuffer + glY1 * width * 4;
        uint8_t* uvOut = uvPlane + y * width;

        for (int x = 0; x < width / 2; x++) {
            int x0 = x * 2;
            int x1 = x * 2 + 1;

            // 平均 4 个像素的 RGB
            int r = (rgba0[x0 * 4 + 0] + rgba0[x1 * 4 + 0] + rgba1[x0 * 4 + 0] + rgba1[x1 * 4 + 0]) / 4;
            int g = (rgba0[x0 * 4 + 1] + rgba0[x1 * 4 + 1] + rgba1[x0 * 4 + 1] + rgba1[x1 * 4 + 1]) / 4;
            int b = (rgba0[x0 * 4 + 2] + rgba0[x1 * 4 + 2] + rgba1[x0 * 4 + 2] + rgba1[x1 * 4 + 2]) / 4;

            // RGB to UV (BT.601)
            uvOut[x * 2 + 0] = (uint8_t)((-0.148f * r - 0.291f * g + 0.439f * b) + 128.0f);  // U
            uvOut[x * 2 + 1] = (uint8_t)((0.439f * r - 0.368f * g - 0.071f * b) + 128.0f);   // V
        }
    }

    free(rgbaBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    *outNV12Data = nv12Data;
    *outNV12Size = nv12Size;

    return 0;
}

int TimelineGLRender::RenderStretchToFBO(GLuint rgbaTexture, GLuint outputFBO) {
    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLfloat flippedTexCoords[] = {
        0.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_rgbaVBO[1]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(flippedTexCoords), flippedTexCoords);

    glUseProgram(m_rgbaProgram);
    glBindVertexArray(m_rgbaVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rgbaTexture);
    glUniform1i(m_rgbaTextureLoc, 0);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    static const GLfloat originalTexCoords[] = {
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
        1.0f, 0.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_rgbaVBO[1]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(originalTexCoords), originalTexCoords);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
}

int TimelineGLRender::InitBlurRenderer() {
    static const char* BLUR_VERTEX_SHADER_H = 
        "#version 300 es\n"
        "layout(location = 0) in vec4 a_position;\n"
        "layout(location = 1) in vec2 a_texCoord;\n"
        "uniform float u_radius;\n"
        "out vec2 v_texCoord;\n"
        "out vec2 v_blurCoords[14];\n"
        "void main() {\n"
        "    gl_Position = a_position;\n"
        "    v_texCoord = a_texCoord;\n"
        "    vec2 offset = vec2(u_radius / 7.0, 0.0);\n"
        "    for (int i = 0; i < 7; i++) {\n"
        "        v_blurCoords[i] = v_texCoord - offset * float(7 - i);\n"
        "        v_blurCoords[7 + i] = v_texCoord + offset * float(i);\n"
        "    }\n"
        "}\n";

    static const char* BLUR_VERTEX_SHADER_V = 
        "#version 300 es\n"
        "layout(location = 0) in vec4 a_position;\n"
        "layout(location = 1) in vec2 a_texCoord;\n"
        "uniform float u_radius;\n"
        "out vec2 v_texCoord;\n"
        "out vec2 v_blurCoords[14];\n"
        "void main() {\n"
        "    gl_Position = a_position;\n"
        "    v_texCoord = a_texCoord;\n"
        "    vec2 offset = vec2(0.0, u_radius / 7.0);\n"
        "    for (int i = 0; i < 7; i++) {\n"
        "        v_blurCoords[i] = v_texCoord - offset * float(7 - i);\n"
        "        v_blurCoords[7 + i] = v_texCoord + offset * float(i);\n"
        "    }\n"
        "}\n";

    static const char* BLUR_FRAGMENT_SHADER = 
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 v_texCoord;\n"
        "in vec2 v_blurCoords[14];\n"
        "uniform sampler2D s_texture;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "void main() {\n"
        "    float weights[7] = float[](0.00443, 0.00896, 0.02160, 0.04437, 0.07767, 0.11588, 0.14731);\n"
        "    outColor = texture(s_texture, v_texCoord) * 0.15958;\n"
        "    for (int i = 0; i < 7; i++) {\n"
        "        outColor += texture(s_texture, v_blurCoords[i]) * weights[i];\n"
        "        outColor += texture(s_texture, v_blurCoords[7 + i]) * weights[i];\n"
        "    }\n"
        "}\n";

    m_blurProgramH = GLUtils::CreateProgram(BLUR_VERTEX_SHADER_H, BLUR_FRAGMENT_SHADER);
    m_blurProgramV = GLUtils::CreateProgram(BLUR_VERTEX_SHADER_V, BLUR_FRAGMENT_SHADER);
    
    if (m_blurProgramH == 0 || m_blurProgramV == 0) {
        LOGCATE("%s: Create blur programs failed", TAG);
        return -1;
    }

    m_blurPosLoc = glGetAttribLocation(m_blurProgramH, "a_position");
    m_blurTexCoordLoc = glGetAttribLocation(m_blurProgramH, "a_texCoord");
    m_blurTextureLoc = glGetUniformLocation(m_blurProgramH, "s_texture");
    m_blurRadiusLoc = glGetUniformLocation(m_blurProgramH, "u_radius");

    glGenVertexArrays(1, &m_blurVAO);
    glGenBuffers(2, m_blurVBO);
    glGenBuffers(1, &m_blurEBO);

    glBindVertexArray(m_blurVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_blurVBO[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verticesCoords), verticesCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_blurPosLoc);
    glVertexAttribPointer(m_blurPosLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, m_blurVBO[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoords), textureCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_blurTexCoordLoc);
    glVertexAttribPointer(m_blurTexCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_blurEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    LOGCATE("%s: Blur renderer initialized (H=%d, V=%d)", TAG, m_blurProgramH, m_blurProgramV);
    return 0;
}

void TimelineGLRender::UninitBlurRenderer() {
    if (m_blurProgramH) {
        glDeleteProgram(m_blurProgramH);
        m_blurProgramH = 0;
    }
    if (m_blurProgramV) {
        glDeleteProgram(m_blurProgramV);
        m_blurProgramV = 0;
    }
    if (m_blurVAO) {
        glDeleteVertexArrays(1, &m_blurVAO);
        m_blurVAO = 0;
    }
    if (m_blurVBO[0] || m_blurVBO[1]) {
        glDeleteBuffers(2, m_blurVBO);
        memset(m_blurVBO, 0, sizeof(m_blurVBO));
    }
    if (m_blurEBO) {
        glDeleteBuffers(1, &m_blurEBO);
        m_blurEBO = 0;
    }
}

int TimelineGLRender::ApplyGaussianBlur(GLuint inputTexture, GLuint outputFBO, int width, int height, float radius) {
    GLuint tempTexture = CreateTexture();
    glBindTexture(GL_TEXTURE_2D, tempTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    
    GLuint tempFBO = 0;
    glGenFramebuffers(1, &tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tempTexture, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGCATE("%s: Temp FBO not complete for blur", TAG);
        glDeleteTextures(1, &tempTexture);
        glDeleteFramebuffers(1, &tempFBO);
        return -1;
    }

    float texelWidth = 1.0f / width;
    float texelHeight = 1.0f / height;
    float blurRadius = radius * texelWidth;

    glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_blurProgramH);
    glBindVertexArray(m_blurVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(m_blurTextureLoc, 0);
    glUniform1f(m_blurRadiusLoc, blurRadius);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);

    blurRadius = radius * texelHeight;

    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_blurProgramV);
    glBindVertexArray(m_blurVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tempTexture);
    glUniform1i(m_blurTextureLoc, 0);
    glUniform1f(m_blurRadiusLoc, blurRadius);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDeleteTextures(1, &tempTexture);
    glDeleteFramebuffers(1, &tempFBO);

    LOGCATE("%s: Applied Gaussian blur (radius=%.1f)", TAG, radius);
    return 0;
}

int TimelineGLRender::InitYUVRenderer() {
    m_yuvProgram = GLUtils::CreateProgram(YUV_VERTEX_SHADER, YUV_FRAGMENT_SHADER);
    if (m_yuvProgram == 0) {
        LOGCATE("%s: Create YUV program failed", TAG);
        return -1;
    }

    m_yuvPosLoc = glGetAttribLocation(m_yuvProgram, "a_position");
    m_yuvTexCoordLoc = glGetAttribLocation(m_yuvProgram, "a_texCoord");
    m_yuvYTexLoc = glGetUniformLocation(m_yuvProgram, "s_yTexture");
    m_yuvUVTexLoc = glGetUniformLocation(m_yuvProgram, "s_uvTexture");

    glGenVertexArrays(1, &m_yuvVAO);
    glGenBuffers(2, m_yuvVBO);
    glGenBuffers(1, &m_yuvEBO);

    glBindVertexArray(m_yuvVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_yuvVBO[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verticesCoords), verticesCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_yuvPosLoc);
    glVertexAttribPointer(m_yuvPosLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, m_yuvVBO[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoords), textureCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(m_yuvTexCoordLoc);
    glVertexAttribPointer(m_yuvTexCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_yuvEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    // 创建Y和UV纹理
    glGenTextures(1, &m_yTexture);
    glBindTexture(GL_TEXTURE_2D, m_yTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_uvTexture);
    glBindTexture(GL_TEXTURE_2D, m_uvTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    LOGCATE("%s: YUV renderer initialized", TAG);
    return 0;
}

void TimelineGLRender::UninitYUVRenderer() {
    if (m_yuvProgram) {
        glDeleteProgram(m_yuvProgram);
        m_yuvProgram = 0;
    }
    if (m_yuvVAO) {
        glDeleteVertexArrays(1, &m_yuvVAO);
        m_yuvVAO = 0;
    }
    glDeleteBuffers(2, m_yuvVBO);
    glDeleteBuffers(1, &m_yuvEBO);
    
    if (m_yTexture) {
        glDeleteTextures(1, &m_yTexture);
        m_yTexture = 0;
    }
    if (m_uvTexture) {
        glDeleteTextures(1, &m_uvTexture);
        m_uvTexture = 0;
    }
    
    LOGCATE("%s: YUV renderer uninitialized", TAG);
}

int TimelineGLRender::InitReusableResources() {
    // 预分配缓冲区
    m_yBufferSize = m_width * m_height;
    m_uvBufferSize = m_width * m_height / 2;
    
    m_yBuffer = (uint8_t*)malloc(m_yBufferSize);
    m_uvBuffer = (uint8_t*)malloc(m_uvBufferSize);
    
    if (!m_yBuffer || !m_uvBuffer) {
        LOGCATE("%s: Failed to allocate buffers", TAG);
        return -1;
    }

    // 创建复用的纹理
    glGenTextures(1, &m_texture1);
    glBindTexture(GL_TEXTURE_2D, m_texture1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &m_texture2);
    glBindTexture(GL_TEXTURE_2D, m_texture2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &m_texture3);
    glBindTexture(GL_TEXTURE_2D, m_texture3);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // 创建复用的FBO
    glGenFramebuffers(1, &m_fbo1);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture1, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGCATE("%s: FBO 1 not complete", TAG);
        return -1;
    }

    glGenFramebuffers(1, &m_fbo2);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture2, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGCATE("%s: FBO 2 not complete", TAG);
        return -1;
    }

    glGenFramebuffers(1, &m_fbo3);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo3);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture3, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGCATE("%s: FBO 3 not complete", TAG);
        return -1;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    LOGCATE("%s: Reusable resources initialized", TAG);
    return 0;
}

void TimelineGLRender::UninitReusableResources() {
    if (m_yBuffer) {
        free(m_yBuffer);
        m_yBuffer = nullptr;
    }
    if (m_uvBuffer) {
        free(m_uvBuffer);
        m_uvBuffer = nullptr;
    }
    
    if (m_texture1) glDeleteTextures(1, &m_texture1);
    if (m_texture2) glDeleteTextures(1, &m_texture2);
    if (m_texture3) glDeleteTextures(1, &m_texture3);
    if (m_fbo1) glDeleteFramebuffers(1, &m_fbo1);
    if (m_fbo2) glDeleteFramebuffers(1, &m_fbo2);
    if (m_fbo3) glDeleteFramebuffers(1, &m_fbo3);
    
    m_texture1 = m_texture2 = m_texture3 = 0;
    m_fbo1 = m_fbo2 = m_fbo3 = 0;
    
    LOGCATE("%s: Reusable resources uninitialized", TAG);
}

int TimelineGLRender::UploadYUVToTextures(AVFrame* avFrame) {
    if (!avFrame) return -1;
    
    int srcW = avFrame->width;
    int srcH = avFrame->height;
    AVPixelFormat pixFormat = (AVPixelFormat)avFrame->format;
    
    // 根据格式提取Y和UV数据
    if (pixFormat == AV_PIX_FMT_YUV420P || pixFormat == AV_PIX_FMT_YUVJ420P) {
        // YUV420P/YUVJ420P: Y, U, V 分别存储
        glBindTexture(GL_TEXTURE_2D, m_yTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, srcW, srcH, 0, GL_RED, GL_UNSIGNED_BYTE, avFrame->data[0]);
        
        // 合并UV为NV12格式
        uint8_t* uv = m_uvBuffer;
        uint8_t* u = avFrame->data[1];
        uint8_t* v = avFrame->data[2];
        int uvSize = srcW * srcH / 4;
        for (int i = 0; i < uvSize; i++) {
            uv[i * 2] = u[i];
            uv[i * 2 + 1] = v[i];
        }
        
        glBindTexture(GL_TEXTURE_2D, m_uvTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, srcW / 2, srcH / 2, 0, GL_RG, GL_UNSIGNED_BYTE, uv);
    } else if (pixFormat == AV_PIX_FMT_NV12) {
        // NV12: Y单独存储，UV交错存储
        glBindTexture(GL_TEXTURE_2D, m_yTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, srcW, srcH, 0, GL_RED, GL_UNSIGNED_BYTE, avFrame->data[0]);
        
        glBindTexture(GL_TEXTURE_2D, m_uvTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, srcW / 2, srcH / 2, 0, GL_RG, GL_UNSIGNED_BYTE, avFrame->data[1]);
    } else {
        LOGCATE("%s: Unsupported pixel format: %d", TAG, avFrame->format);
        return -1;
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

int TimelineGLRender::RenderYUVToFBO(GLuint outputFBO, int dstW, int dstH, int offsetX, int offsetY) {
    float left = (2.0f * offsetX / m_width) - 1.0f;
    float right = (2.0f * (offsetX + dstW) / m_width) - 1.0f;
    float bottom = (2.0f * offsetY / m_height) - 1.0f;
    float top = (2.0f * (offsetY + dstH) / m_height) - 1.0f;

    GLfloat vertices[] = {
        left,  top,
        left,  bottom,
        right, bottom,
        right, top,
    };

    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, m_yuvVBO[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glUseProgram(m_yuvProgram);
    glBindVertexArray(m_yuvVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_yTexture);
    glUniform1i(m_yuvYTexLoc, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_uvTexture);
    glUniform1i(m_yuvUVTexLoc, 1);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 恢复原始顶点数据
    static const GLfloat originalVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_yuvVBO[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(originalVertices), originalVertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
}

int TimelineGLRender::RenderFrameToSurface(AVFrame* avFrame, ANativeWindow* window) {
    if (!avFrame || !window) {
        LOGCATE("%s: Invalid frame or window", TAG);
        return -1;
    }
    
    int srcW = avFrame->width;
    int srcH = avFrame->height;
    
    // 获取窗口大小
    int winW = ANativeWindow_getWidth(window);
    int winH = ANativeWindow_getHeight(window);
    
    if (winW <= 0 || winH <= 0) {
        LOGCATE("%s: Invalid window size: %dx%d", TAG, winW, winH);
        return -1;
    }
    
    // 设置缓冲区大小
    int32_t result = ANativeWindow_setBuffersGeometry(window, winW, winH, WINDOW_FORMAT_RGBA_8888);
    if (result < 0) {
        LOGCATE("%s: Failed to set buffer geometry: %d", TAG, result);
        return -1;
    }
    
    // 锁定窗口缓冲区
    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(window, &buffer, nullptr) != 0) {
        LOGCATE("%s: Failed to lock window", TAG);
        return -1;
    }
    
    // 使用中间缓冲区来处理stride不一致的情况
    int dstStride = buffer.stride * 4;
    uint8_t* dstBuffer = (uint8_t*)malloc(winW * winH * 4);
    if (!dstBuffer) {
        LOGCATE("%s: Failed to allocate temp buffer", TAG);
        ANativeWindow_unlockAndPost(window);
        return -1;
    }
    
    // 使用sws_scale将视频帧转换为RGBA格式
    SwsContext* swsCtx = sws_getContext(
        srcW, srcH, (AVPixelFormat)avFrame->format,
        winW, winH, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!swsCtx) {
        LOGCATE("%s: Failed to create sws context", TAG);
        free(dstBuffer);
        ANativeWindow_unlockAndPost(window);
        return -1;
    }
    
    // 准备目标数据指针 - 使用临时缓冲区
    uint8_t* dstData[4] = { dstBuffer, nullptr, nullptr, nullptr };
    int dstLinesize[4] = { winW * 4, 0, 0, 0 };
    
    // 执行转换
    sws_scale(swsCtx, avFrame->data, avFrame->linesize, 0, srcH, dstData, dstLinesize);
    sws_freeContext(swsCtx);
    
    // 将临时缓冲区的数据复制到窗口缓冲区（处理stride）
    uint8_t* srcPtr = dstBuffer;
    uint8_t* dstPtr = (uint8_t*)buffer.bits;
    int rowBytes = winW * 4;
    for (int y = 0; y < winH; y++) {
        memcpy(dstPtr, srcPtr, rowBytes);
        srcPtr += rowBytes;
        dstPtr += dstStride;
    }
    
    free(dstBuffer);
    
    // 解锁并提交缓冲区
    ANativeWindow_unlockAndPost(window);
    
    return 0;
}

int TimelineGLRender::RenderYUVToFBOWithBlend(GLuint outputFBO, int dstW, int dstH, int offsetX, int offsetY) {
    float left = (2.0f * offsetX / m_width) - 1.0f;
    float right = (2.0f * (offsetX + dstW) / m_width) - 1.0f;
    float bottom = (2.0f * offsetY / m_height) - 1.0f;
    float top = (2.0f * (offsetY + dstH) / m_height) - 1.0f;

    GLfloat vertices[] = {
        left,  top,
        left,  bottom,
        right, bottom,
        right, top,
    };

    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, m_width, m_height);

    // 不清除背景，保留已有内容
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, m_yuvVBO[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glUseProgram(m_yuvProgram);
    glBindVertexArray(m_yuvVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_yTexture);
    glUniform1i(m_yuvYTexLoc, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_uvTexture);
    glUniform1i(m_yuvUVTexLoc, 1);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 恢复原始顶点数据
    static const GLfloat originalVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_yuvVBO[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(originalVertices), originalVertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
}
