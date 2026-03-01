#ifndef LEARNFFMPEG_TIMELINEENCODER_H
#define LEARNFFMPEG_TIMELINEENCODER_H

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>
#include <media/NdkMediaFormat.h>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "ImageDef.h"
#include "Timeline.h"
#include "TimelineGLRender.h"
#include "CPUWatermarkFilter.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

enum class EncoderState {
    ENCODER_STATE_IDLE,
    ENCODER_STATE_READY,
    ENCODER_STATE_ENCODING,
    ENCODER_STATE_ERROR
};

struct EncoderConfig {
    int width;
    int height;
    int frameRate;
    int64_t bitRate;
    int iframeInterval;
    std::string outputPath;
    int colorFormat;

    EncoderConfig()
        : width(1920)
        , height(1080)
        , frameRate(30)
        , bitRate(8000000)
        , iframeInterval(1)
        , colorFormat(21)
    {}
};

typedef void (*EncodeProgressCallback)(void* context, int progress, int total);
typedef void (*EncodeCompleteCallback)(void* context, int result);
typedef int (*EncodeFilterCallback)(void* context, AVFrame* inputFrame, AVFrame** outputFrame);

class TimelineEncoder {
public:
    TimelineEncoder();
    ~TimelineEncoder();

    int Init(const char* outputPath, int width, int height, int frameRate, int64_t bitRate);
    int Init(const EncoderConfig& config);
    void UnInit();

    int StartEncoding(Timeline* timeline);
    void StopEncoding();
    void PauseEncoding();
    void ResumeEncoding();

    int EncodeFrame(NativeImage* frame, int64_t presentationTimeUs);
    int Finalize();

    EncoderState GetState() const { return m_state; }
    float GetProgress() const;
    int64_t GetEncodedFrameCount() const { return m_encodedFrameCount; }
    int64_t GetTotalFrameCount() const { return m_totalFrameCount; }

    void SetProgressCallback(void* context, EncodeProgressCallback callback) {
        m_progressContext = context;
        m_progressCallback = callback;
    }

    void SetCompleteCallback(void* context, EncodeCompleteCallback callback) {
        m_completeContext = context;
        m_completeCallback = callback;
    }

    void SetFilterCallback(void* context, EncodeFilterCallback callback) {
        m_filterContext = context;
        m_filterCallback = callback;
    }

    // CPU 水印支持
    void SetWatermark(const char* imagePath, int x, int y, float opacity, float scale);
    void EnableWatermark(bool enable) { m_watermarkEnabled = enable; }

private:
    int CreateEncoder();
    void DestroyEncoder();
    int CreateMuxer();
    void DestroyMuxer();

    int ConfigureEncoder();
    int StartEncoder();
    int DrainEncoder(bool endOfStream);

    int ConvertFrameToNV12(AVFrame* srcFrame, uint8_t** dstData, int* dstSize);
    int FeedEncoderInput(uint8_t* data, int size, int64_t presentationTimeUs);

    // OpenGL 渲染
    int RenderFrameWithOpenGL(AVFrame* frame, uint8_t** outNV12Data, int* outNV12Size);

    static void EncodeThreadFunc(TimelineEncoder* encoder);
    int ProcessTimelineFrame(Timeline* timeline, int64_t frameIndex, int64_t startFrameIndex = 0);

private:
    EncoderConfig m_config;
    EncoderState m_state;

    AMediaCodec* m_videoCodec;
    AMediaMuxer* m_mediaMuxer;
    AMediaFormat* m_outputFormat;
    int m_videoTrackIndex;
    int64_t m_frameIndex;
    int64_t m_encodedFrameCount;
    int64_t m_totalFrameCount;

    SwsContext* m_swsContext;
    AVFrame* m_yuvFrame;
    uint8_t* m_nv12Buffer;

    std::thread* m_encodeThread;
    std::mutex m_mutex;
    std::condition_variable m_condVar;
    bool m_isEncoding;
    bool m_isPaused;
    bool m_stopRequested;

    void* m_progressContext;
    EncodeProgressCallback m_progressCallback;
    void* m_completeContext;
    EncodeCompleteCallback m_completeCallback;
    void* m_filterContext;
    EncodeFilterCallback m_filterCallback;

    Timeline* m_currentTimeline;
    
    // OpenGL 渲染器
    TimelineGLRender* m_glRender;
    bool m_useOpenGL;
    
    // 保存的水印信息（用于 OpenGL 初始化后设置）
    std::string m_pendingWatermarkPath;
    float m_pendingWatermarkOpacity;
    float m_pendingWatermarkScale;
    float m_pendingWatermarkOffsetX;
    float m_pendingWatermarkOffsetY;
    bool m_pendingWatermarkSet;
    
    // CPU 水印（fallback）
    CPUWatermarkFilter* m_cpuWatermark;
    bool m_watermarkEnabled;
    
    // 编码状态跟踪（替代静态变量，确保每次导出重置）
    TimelineClip* m_lastProcessedClip;
    int64_t m_lastProcessedTime;
    int m_lastProcessedClipIndex;
    
    // 帧缓存（替代静态变量）
    AVFrame* m_lastFrameCache;
    AVFrame* m_eofFrameCache;
    int64_t m_lastPresentationPts;
};

#endif
