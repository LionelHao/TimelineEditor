#ifndef LEARNFFMPEG_WATERMARKFILTER_H
#define LEARNFFMPEG_WATERMARKFILTER_H

#include "VideoFilter.h"
#include <string>

enum class WatermarkPosition {
    POSITION_TOP_LEFT = 0,
    POSITION_TOP_RIGHT,
    POSITION_BOTTOM_LEFT,
    POSITION_BOTTOM_RIGHT,
    POSITION_CENTER,
    POSITION_CUSTOM
};

struct WatermarkConfig {
    std::string imagePath;
    WatermarkPosition position;
    float opacity;
    float scale;
    float offsetX;
    float offsetY;
    int width;
    int height;

    WatermarkConfig()
        : position(WatermarkPosition::POSITION_BOTTOM_RIGHT)
        , opacity(0.8f)
        , scale(0.2f)
        , offsetX(0.05f)
        , offsetY(0.05f)
        , width(0)
        , height(0)
    {}
};

class WatermarkFilter : public VideoFilter {
public:
    WatermarkFilter();
    virtual ~WatermarkFilter();

    int Init() override;
    void UnInit() override;

    void Apply(GLuint inputTexture, GLuint outputFBO, int width, int height) override;

    FilterType GetType() const override { return FilterType::FILTER_TYPE_WATERMARK; }
    const char* GetName() const override { return "WatermarkFilter"; }

    int SetWatermarkImage(const char* imagePath);
    int SetWatermarkImage(NativeImage* image);
    void SetPosition(WatermarkPosition position);
    void SetOpacity(float opacity);
    void SetScale(float scale);
    void SetOffset(float offsetX, float offsetY);

    void SetConfig(const WatermarkConfig& config) { m_config = config; }
    const WatermarkConfig& GetConfig() const { return m_config; }

private:
    int CreateWatermarkTexture();
    void CalculatePosition(int videoWidth, int videoHeight, 
                          float& x, float& y, float& w, float& h);
    int LoadImageWithFFmpeg(const char* imagePath, NativeImage* image);

private:
    WatermarkConfig m_config;
    GLuint m_watermarkTexture;
    NativeImage m_watermarkImage;
    bool m_hasWatermark;

    GLint m_positionLoc;
    GLint m_texCoordLoc;
    GLint m_inputTextureLoc;
    GLint m_watermarkTextureLoc;
    GLint m_watermarkRectLoc;
    GLint m_opacityLoc;
};

#endif
