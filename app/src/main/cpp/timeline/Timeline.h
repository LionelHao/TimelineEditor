#ifndef LEARNFFMPEG_TIMELINE_H
#define LEARNFFMPEG_TIMELINE_H

#include <vector>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include "TimelineClip.h"

enum class TimelineState {
    TIMELINE_STATE_IDLE,
    TIMELINE_STATE_READY,
    TIMELINE_STATE_EXPORTING,
    TIMELINE_STATE_ERROR
};

struct TimelineConfig {
    int width;
    int height;
    int frameRate;
    int64_t bitRate;
    std::string outputPath;
};

class Timeline {
public:
    Timeline();
    ~Timeline();

    int Init(const TimelineConfig& config);
    void UnInit();

    int AddClip(const char* sourcePath, int64_t position = -1);
    int AddClipAtIndex(const char* sourcePath, int index);
    int RemoveClip(int index);
    int MoveClip(int fromIndex, int toIndex);
    int SwapClips(int index1, int index2);

    TimelineClip* GetClip(int index);
    TimelineClip* GetClipAtTime(int64_t timestamp);
    int GetClipIndex(TimelineClip* clip);

    int GetClipCount() const { return static_cast<int>(m_clips.size()); }
    int64_t GetDuration() const;
    int64_t GetFrameCount() const;

    int64_t GetFrameTimestamp(int64_t frameIndex) const;

    const TimelineConfig& GetConfig() const { return m_config; }
    TimelineState GetState() const { return m_state; }

    void SetOutputPath(const std::string& path) { m_config.outputPath = path; }
    std::string GetOutputPath() const { return m_config.outputPath; }

    int GetWidth() const { return m_config.width; }
    int GetHeight() const { return m_config.height; }
    int GetFrameRate() const { return m_config.frameRate; }

    void Lock() { m_mutex.lock(); }
    void Unlock() { m_mutex.unlock(); }

    void PrintTimelineInfo();

private:
    void RecalculateDuration();
    int64_t FindNextClipPosition();

private:
    std::vector<TimelineClip*> m_clips;
    TimelineConfig m_config;
    TimelineState m_state;
    int64_t m_duration;
    std::mutex m_mutex;
};

#endif
