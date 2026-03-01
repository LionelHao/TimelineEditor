#include "VideoFilter.h"
#include "LogUtil.h"
#include <cstring>

#define TAG "VideoFilter"

static const char* DEFAULT_VERTEX_SHADER = 
    "#version 300 es\n"
    "layout(location = 0) in vec4 a_position;\n"
    "layout(location = 1) in vec2 a_texCoord;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = a_position;\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

static const GLfloat VERTEX_COORDS[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};

static const GLfloat TEXTURE_COORDS[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
};

VideoFilter::VideoFilter()
    : m_initialized(false)
    , m_program(0)
    , m_vao(0)
{
    memset(m_vbo, 0, sizeof(m_vbo));
}

VideoFilter::~VideoFilter() {
    // 不要在析构函数中调用纯虚函数
    // 子类应该在析构时自己调用UnInit
    if (m_initialized) {
        // 只清理基类资源
        if (m_program != 0) {
            glDeleteProgram(m_program);
            m_program = 0;
        }
        if (m_vao != 0) {
            glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }
        for (int i = 0; i < 2; i++) {
            if (m_vbo[i] != 0) {
                glDeleteBuffers(1, &m_vbo[i]);
                m_vbo[i] = 0;
            }
        }
        m_initialized = false;
    }
}

GLuint VideoFilter::LoadShader(GLenum type, const char* shaderSource) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        LOGCATE("%s: glCreateShader failed", TAG);
        return 0;
    }

    glShaderSource(shader, 1, &shaderSource, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* log = new char[infoLen];
            glGetShaderInfoLog(shader, infoLen, nullptr, log);
            LOGCATE("%s: Shader compile error: %s", TAG, log);
            delete[] log;
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint VideoFilter::CreateProgram(const char* vertexShader, const char* fragmentShader) {
    GLuint vs = LoadShader(GL_VERTEX_SHADER, vertexShader);
    if (vs == 0) {
        LOGCATE("%s: Load vertex shader failed", TAG);
        return 0;
    }

    GLuint fs = LoadShader(GL_FRAGMENT_SHADER, fragmentShader);
    if (fs == 0) {
        LOGCATE("%s: Load fragment shader failed", TAG);
        glDeleteShader(vs);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        LOGCATE("%s: glCreateProgram failed", TAG);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* log = new char[infoLen];
            glGetProgramInfoLog(program, infoLen, nullptr, log);
            LOGCATE("%s: Program link error: %s", TAG, log);
            delete[] log;
        }
        glDeleteProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}

void VideoFilter::CheckGLError(const char* operation) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGCATE("%s: GL error after %s: 0x%x", TAG, operation, error);
    }
}

void VideoFilter::Apply(NativeImage* input, NativeImage* output) {
}
