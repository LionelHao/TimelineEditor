#include "HistogramEqualizationFilter.h"
#include "LogUtil.h"
#include <cstring>
#include <algorithm>

#define TAG "HistogramEqualizationFilter"

static const char* HISTOGRAM_VERTEX_SHADER = 
    "#version 300 es\n"
    "layout(location = 0) in vec4 a_position;\n"
    "layout(location = 1) in vec2 a_texCoord;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = a_position;\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

static const char* HISTOGRAM_FRAGMENT_SHADER = 
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_texCoord;\n"
    "layout(location = 0) out vec4 outColor;\n"
    "uniform sampler2D u_inputTexture;\n"
    "uniform float u_intensity;\n"
    "\n"
    "float luminance(vec3 color) {\n"
    "    return dot(color, vec3(0.299, 0.587, 0.114));\n"
    "}\n"
    "\n"
    "vec3 rgb2hsv(vec3 c) {\n"
    "    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
    "    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));\n"
    "    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));\n"
    "    float d = q.x - min(q.w, q.y);\n"
    "    float e = 1.0e-10;\n"
    "    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);\n"
    "}\n"
    "\n"
    "vec3 hsv2rgb(vec3 c) {\n"
    "    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
    "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
    "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 color = texture(u_inputTexture, v_texCoord);\n"
    "    \n"
    "    vec3 hsv = rgb2hsv(color.rgb);\n"
    "    \n"
    "    float v = hsv.z;\n"
    "    \n"
    "    float vEqualized = v;\n"
    "    \n"
    "    float localContrast = 0.0;\n"
    "    for (int i = -1; i <= 1; i++) {\n"
    "        for (int j = -1; j <= 1; j++) {\n"
    "            vec2 offset = vec2(float(i), float(j)) * 0.001;\n"
    "            float neighborLum = luminance(texture(u_inputTexture, v_texCoord + offset).rgb);\n"
    "            localContrast += abs(v - neighborLum);\n"
    "        }\n"
    "    }\n"
    "    localContrast /= 9.0;\n"
    "    \n"
    "    float adaptiveIntensity = u_intensity * (1.0 - localContrast * 2.0);\n"
    "    adaptiveIntensity = clamp(adaptiveIntensity, 0.0, 1.0);\n"
    "    \n"
    "    vEqualized = pow(v, 0.8 + adaptiveIntensity * 0.4);\n"
    "    \n"
    "    hsv.z = mix(v, vEqualized, adaptiveIntensity);\n"
    "    \n"
    "    vec3 result = hsv2rgb(hsv);\n"
    "    \n"
    "    outColor = vec4(mix(color.rgb, result, u_intensity), color.a);\n"
    "}\n";

HistogramEqualizationFilter::HistogramEqualizationFilter()
    : m_intensity(1.0f)
    , m_histogramTexture(0)
    , m_lutTexture(0)
    , m_positionLoc(-1)
    , m_texCoordLoc(-1)
    , m_inputTextureLoc(-1)
    , m_intensityLoc(-1)
{
}

HistogramEqualizationFilter::~HistogramEqualizationFilter() {
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

    if (m_histogramTexture) {
        glDeleteTextures(1, &m_histogramTexture);
        m_histogramTexture = 0;
    }

    if (m_lutTexture) {
        glDeleteTextures(1, &m_lutTexture);
        m_lutTexture = 0;
    }

    m_initialized = false;
}

int HistogramEqualizationFilter::Init() {
    if (m_initialized) {
        return 0;
    }

    m_program = CreateProgram(HISTOGRAM_VERTEX_SHADER, HISTOGRAM_FRAGMENT_SHADER);
    if (m_program == 0) {
        LOGCATE("%s: CreateProgram failed", TAG);
        return -1;
    }

    m_positionLoc = glGetAttribLocation(m_program, "a_position");
    m_texCoordLoc = glGetAttribLocation(m_program, "a_texCoord");
    m_inputTextureLoc = glGetUniformLocation(m_program, "u_inputTexture");
    m_intensityLoc = glGetUniformLocation(m_program, "u_intensity");

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
    LOGCATE("%s: HistogramEqualizationFilter initialized", TAG);

    return 0;
}

void HistogramEqualizationFilter::UnInit() {
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

    if (m_histogramTexture) {
        glDeleteTextures(1, &m_histogramTexture);
        m_histogramTexture = 0;
    }

    if (m_lutTexture) {
        glDeleteTextures(1, &m_lutTexture);
        m_lutTexture = 0;
    }

    m_initialized = false;
}

void HistogramEqualizationFilter::Apply(GLuint inputTexture, GLuint outputFBO, int width, int height) {
    if (!m_initialized) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glViewport(0, 0, width, height);

    glUseProgram(m_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(m_inputTextureLoc, 0);

    glUniform1f(m_intensityLoc, m_intensity);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void HistogramEqualizationFilter::Apply(NativeImage* input, NativeImage* output) {
    if (!input || !output || !input->ppPlane[0]) {
        return;
    }

    if (output->ppPlane[0] == nullptr) {
        output->width = input->width;
        output->height = input->height;
        output->format = input->format;
        NativeImageUtil::AllocNativeImage(output);
    }

    memcpy(output->ppPlane[0], input->ppPlane[0], 
           input->width * input->height * 4);

    ApplyCPU(output);
}

void HistogramEqualizationFilter::ApplyCPU(NativeImage* image) {
    if (!image || !image->ppPlane[0]) {
        return;
    }

    int width = image->width;
    int height = image->height;
    uint8_t* data = image->ppPlane[0];
    int size = width * height;

    int histogramR[256] = {0};
    int histogramG[256] = {0};
    int histogramB[256] = {0};

    for (int i = 0; i < size; i++) {
        int r = data[i * 4 + 0];
        int g = data[i * 4 + 1];
        int b = data[i * 4 + 2];
        histogramR[r]++;
        histogramG[g]++;
        histogramB[b]++;
    }

    int cdfR[256], cdfG[256], cdfB[256];
    CalculateCDF(histogramR, cdfR, size);
    CalculateCDF(histogramG, cdfG, size);
    CalculateCDF(histogramB, cdfB, size);

    int minCDFR = size, minCDFG = size, minCDFB = size;
    for (int i = 0; i < 256; i++) {
        if (cdfR[i] > 0 && cdfR[i] < minCDFR) minCDFR = cdfR[i];
        if (cdfG[i] > 0 && cdfG[i] < minCDFG) minCDFG = cdfG[i];
        if (cdfB[i] > 0 && cdfB[i] < minCDFB) minCDFB = cdfB[i];
    }

    uint8_t lutR[256], lutG[256], lutB[256];
    for (int i = 0; i < 256; i++) {
        lutR[i] = (uint8_t)(((cdfR[i] - minCDFR) * 255) / (size - minCDFR));
        lutG[i] = (uint8_t)(((cdfG[i] - minCDFG) * 255) / (size - minCDFG));
        lutB[i] = (uint8_t)(((cdfB[i] - minCDFB) * 255) / (size - minCDFB));
    }

    for (int i = 0; i < size; i++) {
        int idx = i * 4;
        data[idx + 0] = (uint8_t)(data[idx + 0] * (1.0f - m_intensity) + lutR[data[idx + 0]] * m_intensity);
        data[idx + 1] = (uint8_t)(data[idx + 1] * (1.0f - m_intensity) + lutG[data[idx + 1]] * m_intensity);
        data[idx + 2] = (uint8_t)(data[idx + 2] * (1.0f - m_intensity) + lutB[data[idx + 2]] * m_intensity);
    }
}

void HistogramEqualizationFilter::CalculateHistogram(uint8_t* data, int size, int* histogram) {
    memset(histogram, 0, 256 * sizeof(int));
    for (int i = 0; i < size; i++) {
        histogram[data[i]]++;
    }
}

void HistogramEqualizationFilter::CalculateCDF(int* histogram, int* cdf, int totalPixels) {
    cdf[0] = histogram[0];
    for (int i = 1; i < 256; i++) {
        cdf[i] = cdf[i - 1] + histogram[i];
    }
}

void HistogramEqualizationFilter::EqualizeChannel(uint8_t* data, int size, int* cdf, int maxCDF) {
    int minCDF = maxCDF;
    for (int i = 0; i < 256; i++) {
        if (cdf[i] > 0 && cdf[i] < minCDF) {
            minCDF = cdf[i];
        }
    }

    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        lut[i] = (uint8_t)(((cdf[i] - minCDF) * 255) / (maxCDF - minCDF));
    }

    for (int i = 0; i < size; i++) {
        data[i] = lut[data[i]];
    }
}
