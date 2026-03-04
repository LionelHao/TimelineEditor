#include "EGLContextManager.h"
#include "LogUtil.h"

#define TAG "EGLContextManager"

EGLContextManager::EGLContextManager()
    : m_display(EGL_NO_DISPLAY)
    , m_context(EGL_NO_CONTEXT)
    , m_surface(EGL_NO_SURFACE)
    , m_initialized(false)
    , m_width(0)
    , m_height(0)
{
}

EGLContextManager::~EGLContextManager() {
    Uninit();
}

int EGLContextManager::Init(int width, int height) {
    if (m_initialized) {
        LOGCATE("%s: Already initialized", TAG);
        return 0;
    }

    m_width = width;
    m_height = height;

    // 1. 获取 EGLDisplay
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY) {
        LOGCATE("%s: eglGetDisplay failed, error=%x", TAG, eglGetError());
        return -1;
    }

    // 2. 初始化 EGL
    EGLint major, minor;
    if (!eglInitialize(m_display, &major, &minor)) {
        LOGCATE("%s: eglInitialize failed, error=%x", TAG, eglGetError());
        return -1;
    }

    LOGCATE("%s: EGL version: %d.%d", TAG, major, minor);

    // 3. 配置选择
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(m_display, configAttribs, &config, 1, &numConfigs)) {
        LOGCATE("%s: eglChooseConfig failed, error=%x", TAG, eglGetError());
        return -1;
    }

    if (numConfigs == 0) {
        LOGCATE("%s: No configs found", TAG);
        return -1;
    }

    // 4. 创建 EGLSurface（离屏使用 Pbuffer）
    EGLint surfaceAttribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE
    };

    m_surface = eglCreatePbufferSurface(m_display, config, surfaceAttribs);
    if (m_surface == EGL_NO_SURFACE) {
        LOGCATE("%s: eglCreatePbufferSurface failed, error=%x", TAG, eglGetError());
        return -1;
    }

    // 5. 创建 EGLContext
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, contextAttribs);
    if (m_context == EGL_NO_CONTEXT) {
        LOGCATE("%s: eglCreateContext failed, error=%x", TAG, eglGetError());
        return -1;
    }

    // 6. 使上下文有效
    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        LOGCATE("%s: eglMakeCurrent failed, error=%x", TAG, eglGetError());
        return -1;
    }

    m_initialized = true;
    LOGCATE("%s: EGL context initialized successfully (%dx%d)", TAG, width, height);

    // 打印 OpenGL 信息
    LOGCATE("%s: GL_VERSION: %s", TAG, glGetString(GL_VERSION));
    LOGCATE("%s: GL_VENDOR: %s", TAG, glGetString(GL_VENDOR));
    LOGCATE("%s: GL_RENDERER: %s", TAG, glGetString(GL_RENDERER));

    return 0;
}

int EGLContextManager::InitWithExternalContext(EGLContext eglContext, EGLDisplay eglDisplay) {
    if (m_initialized) {
        LOGCATE("%s: Already initialized", TAG);
        return 0;
    }

    m_display = eglDisplay;
    m_context = eglContext;
    m_surface = EGL_NO_SURFACE;
    m_initialized = true;
    
    LOGCATE("%s: EGL context initialized with external context", TAG);
    
    LOGCATE("%s: GL_VERSION: %s", TAG, glGetString(GL_VERSION));
    LOGCATE("%s: GL_VENDOR: %s", TAG, glGetString(GL_VENDOR));
    LOGCATE("%s: GL_RENDERER: %s", TAG, glGetString(GL_RENDERER));
    
    return 0;
}

void EGLContextManager::Uninit() {
    if (!m_initialized) {
        return;
    }

    // 释放当前上下文
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    // 销毁表面
    if (m_surface != EGL_NO_SURFACE) {
        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
    }

    // 销毁上下文
    if (m_context != EGL_NO_CONTEXT) {
        eglDestroyContext(m_display, m_context);
        m_context = EGL_NO_CONTEXT;
    }

    // 终止显示
    if (m_display != EGL_NO_DISPLAY) {
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
    }

    m_initialized = false;
    LOGCATE("%s: EGL context destroyed", TAG);
}

bool EGLContextManager::MakeCurrent() {
    if (!m_initialized) {
        return false;
    }

    if (m_surface == EGL_NO_SURFACE) {
        LOGCATE("%s: MakeCurrent - surface is EGL_NO_SURFACE, skipping", TAG);
        return true;
    }

    return eglMakeCurrent(m_display, m_surface, m_surface, m_context);
}

void EGLContextManager::DoneCurrent() {
    if (m_initialized) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}

void EGLContextManager::SwapBuffers() {
    if (m_initialized && m_surface != EGL_NO_SURFACE) {
        if (!eglSwapBuffers(m_display, m_surface)) {
            LOGCATE("%s: eglSwapBuffers failed, error=%x", TAG, eglGetError());
        }
    }
}
