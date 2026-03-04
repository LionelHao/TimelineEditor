#ifndef LEARNFFMPEG_TIMELINECLIP_H
#define LEARNFFMPEG_TIMELINECLIP_H

#include <string>
#include <vector>
#include <cstdint>
#include "ImageDef.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

enum class ClipState {
    CLIP_STATE_IDLE,
    CLIP_STATE_READY,
    CLIP_STATE_PLAYING,
    CLIP_STATE_ERROR
};

struct ClipInfo {
    std::string sourcePath;
    int64_t sourceStartTime;
    int64_t sourceEndTime;
    int64_t timelineStartTime;
    int width;
    int height;
    int frameRate;
    int64_t duration;
    int rotation;
};

class TimelineClip {
public:
    TimelineClip();
    ~TimelineClip();

    int Init(const char* sourcePath, int64_t timelineStartTime = 0);
    void UnInit();

    int OpenSource();
    void CloseSource();

    int SeekToTime(int64_t timestamp);
    int DecodeFrame(AVFrame** ppFrame);

    ClipInfo GetClipInfo() const { return m_clipInfo; }
    ClipState GetState() const { return m_state; }

    int64_t GetTimelineStartTime() const { return m_clipInfo.timelineStartTime; }
    int64_t GetDuration() const { return m_clipInfo.duration; }
    int64_t GetTimelineEndTime() const { 
        return m_clipInfo.timelineStartTime + m_clipInfo.duration; 
    }

    void SetSourceRange(int64_t startTime, int64_t endTime);
    void SetTimelinePosition(int64_t position);

    int GetWidth() const { return m_clipInfo.width; }
    int GetHeight() const { return m_clipInfo.height; }
    int GetFrameRate() const { return m_clipInfo.frameRate; }

    bool IsFrameAvailable() const;

private:
    int InitDecoder();
    void UpdateClipInfo();

private:
    ClipInfo m_clipInfo;
    ClipState m_state;

    AVFormatContext* m_pFormatCtx;
    AVCodecContext* m_pCodecCtx;
    AVStream* m_pVideoStream;
    int m_videoStreamIndex;

    AVFrame* m_pFrame;
    AVPacket* m_pPacket;
    SwsContext* m_pSwsContext;

    int64_t m_currentPts;
    int64_t m_seekTarget;
    bool m_isOpened;
};

#endif
