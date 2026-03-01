#ifndef LEARNFFMPEG_HISTOGRAMEQUALIZATIONFILTER_H
#define LEARNFFMPEG_HISTOGRAMEQUALIZATIONFILTER_H

#include "VideoFilter.h"

class HistogramEqualizationFilter : public VideoFilter {
public:
    HistogramEqualizationFilter();
    virtual ~HistogramEqualizationFilter();

    int Init() override;
    void UnInit() override;

    void Apply(GLuint inputTexture, GLuint outputFBO, int width, int height) override;
    void Apply(NativeImage* input, NativeImage* output) override;

    FilterType GetType() const override { return FilterType::FILTER_TYPE_HISTOGRAM_EQUALIZATION; }
    const char* GetName() const override { return "HistogramEqualizationFilter"; }

    void SetIntensity(float intensity) { m_intensity = intensity; }
    float GetIntensity() const { return m_intensity; }

    void ApplyCPU(NativeImage* image);

private:
    void CalculateHistogram(uint8_t* data, int size, int* histogram);
    void CalculateCDF(int* histogram, int* cdf, int totalPixels);
    void EqualizeChannel(uint8_t* data, int size, int* cdf, int maxCDF);

private:
    float m_intensity;
    GLuint m_histogramTexture;
    GLuint m_lutTexture;

    GLint m_positionLoc;
    GLint m_texCoordLoc;
    GLint m_inputTextureLoc;
    GLint m_intensityLoc;
};

#endif
