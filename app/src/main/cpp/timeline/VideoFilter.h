#ifndef LEARNFFMPEG_VIDEOFILTER_H
#define LEARNFFMPEG_VIDEOFILTER_H

#include <GLES3/gl3.h>
#include <string>
#include "ImageDef.h"

enum class FilterType {
    FILTER_TYPE_NONE = 0,
    FILTER_TYPE_WATERMARK,
    FILTER_TYPE_HISTOGRAM_EQUALIZATION,
    FILTER_TYPE_CUSTOM
};

class VideoFilter {
public:
    VideoFilter();
    virtual ~VideoFilter();

    virtual int Init() = 0;
    virtual void UnInit() = 0;

    virtual void Apply(GLuint inputTexture, GLuint outputFBO, int width, int height) = 0;
    virtual void Apply(NativeImage* input, NativeImage* output);

    virtual FilterType GetType() const = 0;
    virtual const char* GetName() const = 0;

    bool IsInitialized() const { return m_initialized; }

protected:
    GLuint CreateProgram(const char* vertexShader, const char* fragmentShader);
    GLuint LoadShader(GLenum type, const char* shaderSource);
    void CheckGLError(const char* operation);

protected:
    bool m_initialized;
    GLuint m_program;
    GLuint m_vao;
    GLuint m_vbo[3];
};

#endif
