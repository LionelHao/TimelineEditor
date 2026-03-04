#ifndef LEARNFFMPEG_TIMELINEGLRENDER_H
#define LEARNFFMPEG_TIMELINEGLRENDER_H

#include "EGLContextManager.h"
#include "WatermarkFilter.h"
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class TimelineGLRender {
public:
    TimelineGLRender();
    ~TimelineGLRender();

    int Init();
    int InitWithEGLContext(EGLContext eglContext, EGLDisplay eglDisplay);
    void Uninit();
    void SetSurfaceSize(int width, int height);

    int SetWatermark(const char* imagePath, float opacity, float scale, float offsetX, float offsetY);

    int RenderFrame(AVFrame* avFrame, uint8_t** outNV12Data, int* outNV12Size);
    int RenderFrameToSurface(AVFrame* avFrame, ANativeWindow* window);
    int RenderFrameToWindow(AVFrame* avFrame);

    EGLContextManager* GetEGLContext() { return &m_eglContext; }

private:
    int UploadRGBAToTexture(uint8_t* rgbaData, int width, int height, GLuint* outTexture);
    int UploadYUVToTextures(AVFrame* avFrame);
    int RenderRGBAToFBO(GLuint rgbaTexture, GLuint outputFBO);
    int RenderRGBAToFBOWithOffset(GLuint rgbaTexture, GLuint outputFBO, int textureW, int textureH, int offsetX, int offsetY);
    int RenderYUVToFBO(GLuint outputFBO, int dstW, int dstH, int offsetX, int offsetY);
    int RenderYUVToFBOWithBlend(GLuint outputFBO, int dstW, int dstH, int offsetX, int offsetY);
    int ReadBackNV12(GLuint outputFBO, int width, int height, uint8_t** outNV12Data, int* outNV12Size);
    GLuint CreateTexture();
    GLuint CreateFBO(int width, int height);
    int InitRGBARenderer();
    void UninitRGBARenderer();
    
    int InitBlurRenderer();
    void UninitBlurRenderer();
    int ApplyGaussianBlur(GLuint inputTexture, GLuint outputFBO, int width, int height, float radius);
    int RenderStretchToFBO(GLuint rgbaTexture, GLuint outputFBO);
    
    int InitReusableResources();
    void UninitReusableResources();
    int InitYUVRenderer();
    void UninitYUVRenderer();

private:
    EGLContextManager m_eglContext;
    WatermarkFilter* m_watermarkFilter;
    
    int m_width;
    int m_height;
    bool m_initialized;

    GLuint m_rgbaProgram;
    GLuint m_rgbaVAO;
    GLuint m_rgbaVBO[2];
    GLuint m_rgbaEBO;
    GLint m_rgbaPosLoc;
    GLint m_rgbaTexCoordLoc;
    GLint m_rgbaTextureLoc;

    GLuint m_watermarkTexture;
    bool m_hasWatermark;
    
    GLuint m_blurProgramH;
    GLuint m_blurProgramV;
    GLuint m_blurVAO;
    GLuint m_blurVBO[2];
    GLuint m_blurEBO;
    GLint m_blurPosLoc;
    GLint m_blurTexCoordLoc;
    GLint m_blurTextureLoc;
    GLint m_blurRadiusLoc;
    
    // YUV渲染器
    GLuint m_yuvProgram;
    GLuint m_yuvVAO;
    GLuint m_yuvVBO[2];
    GLuint m_yuvEBO;
    GLint m_yuvPosLoc;
    GLint m_yuvTexCoordLoc;
    GLint m_yuvYTexLoc;
    GLint m_yuvUVTexLoc;
    
    // YUV纹理（复用）
    GLuint m_yTexture;
    GLuint m_uvTexture;
    
    // 复用的FBO和纹理
    GLuint m_fbo1, m_fbo2, m_fbo3;
    GLuint m_texture1, m_texture2, m_texture3;
    
    // 预分配的缓冲区
    uint8_t* m_yBuffer;
    uint8_t* m_uvBuffer;
    int m_yBufferSize;
    int m_uvBufferSize;
};

#endif
