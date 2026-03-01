#ifndef LEARNFFPEG_CPUWATERMARKFILTER_H
#define LEARNFFPEG_CPUWATERMARKFILTER_H

#include <cstdint>
#include <string>

// CPU版本水印滤镜 - 直接在NV12/YUV数据上叠加RGBA水印
class CPUWatermarkFilter {
public:
    CPUWatermarkFilter();
    ~CPUWatermarkFilter();

    // 加载水印图片 (RGBA格式)
    int LoadWatermark(const char* imagePath);
    
    // 设置水印位置和透明度
    void SetPosition(int x, int y);
    void SetOpacity(float opacity);
    void SetScale(float scale);

    // 应用水印到NV12数据
    // nv12Data: NV12格式数据 (Y平面 + UV交错平面)
    // width, height: 视频宽高
    void ApplyToNV12(uint8_t* nv12Data, int width, int height);

    // 应用水印到RGBA数据
    void ApplyToRGBA(uint8_t* rgbaData, int width, int height);

    bool IsValid() const { return m_watermarkData != nullptr; }
    
    int GetWidth() const { return m_watermarkWidth; }
    int GetHeight() const { return m_watermarkHeight; }

private:
    void BlendPixel(uint8_t* dstY, uint8_t* dstUV, int uvIndex, 
                    uint8_t srcR, uint8_t srcG, uint8_t srcB, uint8_t srcA,
                    int x, int y, int width, int height);

private:
    uint8_t* m_watermarkData;
    int m_watermarkWidth;
    int m_watermarkHeight;
    
    int m_positionX;
    int m_positionY;
    float m_opacity;
    float m_scale;
};

#endif // LEARNFFPEG_CPUWATERMARKFILTER_H
