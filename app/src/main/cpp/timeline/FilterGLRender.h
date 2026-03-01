#ifndef LEARNFFMPEG_FILTER_GL_RENDER_H
#define LEARNFFMPEG_FILTER_GL_RENDER_H

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include "ImageDef.h"
#include "VideoFilter.h"
#include "WatermarkFilter.h"
#include "HistogramEqualizationFilter.h"
#include <vector>

class FilterGLRender {
public:
    FilterGLRender();
    ~FilterGLRender();

    int Init(int width, int height);
    void UnInit();

    int MakeCurrent();
    int UnMakeCurrent();

    int CreateSurface();
    int DestroySurface();

    void ApplyFilters(NativeImage* inputImage, NativeImage* outputImage);

    void AddFilter(VideoFilter* filter);
    void RemoveFilter(VideoFilter* filter);
    void ClearFilters();

    bool IsValid() const { return m_initialized; }

private:
    int CreatePbufferSurface();
    int SetupFrameBuffer();
    void CleanupFrameBuffer();

    int CreateTextures(int width, int height);
    void DeleteTextures();

    int CreateFrameBuffer(int width, int height);
    void DeleteFrameBuffer();

    void RenderToTexture(NativeImage* inputImage, GLuint inputTexture);
    void Read