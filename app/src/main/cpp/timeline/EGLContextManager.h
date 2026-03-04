#ifndef LEARNFFMPEG_EGLCONTEXTMANAGER_H
#define LEARNFFMPEG_EGLCONTEXTMANAGER_H

#include <EGL/egl.h>
#include <GLES3/gl3.h>

class EGLContextManager {
public:
    EGLContextManager();
    ~EGLContextManager();

    // 初始化 EGL 上下文（离屏）
    int Init(int width, int height);
    int InitWithExternalContext(EGLContext eglContext, EGLDisplay eglDisplay);
    void Uninit();

    // 使当前线程的 EGL 上下文有效
    bool MakeCurrent();
    void DoneCurrent();

    // 交换缓冲区（离屏渲染不需要，但保留接口）
    void SwapBuffers();

    // 获取 EGL 显示/上下文
    EGLDisplay GetDisplay() const { return m_display; }
    EGLContext GetContext() const { return m_context; }
    EGLSurface GetSurface() const { return m_surface; }

    // 检查是否初始化
    bool IsInitialized() const { return m_initialized; }

private:
    EGLDisplay m_display;
    EGLContext m_context;
    EGLSurface m_surface;
    bool m_initialized;
    int m_width;
    int m_height;
};

#endif // LEARNFFMPEG_EGLCONTEXTMANAGER_H
