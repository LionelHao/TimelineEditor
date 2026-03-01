#include "Timeline.h"
#include "LogUtil.h"

#define TAG "Timeline"

Timeline::Timeline()
    : m_state(TimelineState::TIMELINE_STATE_IDLE)
    , m_duration(0)
{
    m_config.width = 1920;
    m_config.height = 1080;
    m_config.frameRate = 30;
    m_config.bitRate = 8000000;
}

Timeline::~Timeline() {
    UnInit();
}

int Timeline::Init(const TimelineConfig& config) {
    m_config = config;
    m_state = TimelineState::TIMELINE_STATE_READY;
    m_duration = 0;
    
    LOGCATE("%s: Timeline initialized, width=%d, height=%d, fps=%d", 
            TAG, m_config.width, m_config.height, m_config.frameRate);
    
    return 0;
}

void Timeline::UnInit() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto clip : m_clips) {
        if (clip) {
            clip->UnInit();
            delete clip;
        }
    }
    m_clips.clear();
    
    m_duration = 0;
    m_state = TimelineState::TIMELINE_STATE_IDLE;
}

int Timeline::AddClip(const char* sourcePath, int64_t position) {
    if (sourcePath == nullptr) {
        LOGCATE("%s: sourcePath is null", TAG);
        return -1;
    }

    TimelineClip* clip = new TimelineClip();
    if (!clip) {
        LOGCATE("%s: Failed to create TimelineClip", TAG);
        return -1;
    }

    int ret = clip->Init(sourcePath);
    if (ret != 0) {
        LOGCATE("%s: Failed to init clip, ret=%d", TAG, ret);
        delete clip;
        return ret;
    }

    ret = clip->OpenSource();
    if (ret != 0) {
        LOGCATE("%s: Failed to open source, ret=%d", TAG, ret);
        delete clip;
        return ret;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (position < 0) {
        position = FindNextClipPosition();
    }
    clip->SetTimelinePosition(position);

    m_clips.push_back(clip);

    std::sort(m_clips.begin(), m_clips.end(), 
              [](TimelineClip* a, TimelineClip* b) {
                  return a->GetTimelineStartTime() < b->GetTimelineStartTime();
              });

    RecalculateDuration();

    LOGCATE("%s: Added clip at position %lld, duration=%lld, clip count=%zu", 
            TAG, (long long)position, (long long)clip->GetDuration(), m_clips.size());

    PrintTimelineInfo();

    return static_cast<int>(m_clips.size()) - 1;
}

int Timeline::AddClipAtIndex(const char* sourcePath, int index) {
    int64_t position = 0;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index < 0 || index > static_cast<int>(m_clips.size())) {
        LOGCATE("%s: Invalid index %d", TAG, index);
        return -1;
    }

    if (index > 0 && index <= static_cast<int>(m_clips.size())) {
        position = m_clips[index - 1]->GetTimelineEndTime();
    }

    Unlock();
    int ret = AddClip(sourcePath, position);
    Lock();
    
    return ret;
}

int Timeline::RemoveClip(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index < 0 || index >= static_cast<int>(m_clips.size())) {
        LOGCATE("%s: Invalid index %d", TAG, index);
        return -1;
    }

    TimelineClip* clip = m_clips[index];
    m_clips.erase(m_clips.begin() + index);

    if (clip) {
        clip->UnInit();
        delete clip;
    }

    RecalculateDuration();

    LOGCATE("%s: Removed clip at index %d", TAG, index);

    return 0;
}

int Timeline::MoveClip(int fromIndex, int toIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (fromIndex < 0 || fromIndex >= static_cast<int>(m_clips.size()) ||
        toIndex < 0 || toIndex >= static_cast<int>(m_clips.size())) {
        LOGCATE("%s: Invalid indices from=%d, to=%d", TAG, fromIndex, toIndex);
        return -1;
    }

    if (fromIndex == toIndex) {
        return 0;
    }

    TimelineClip* clip = m_clips[fromIndex];
    m_clips.erase(m_clips.begin() + fromIndex);
    m_clips.insert(m_clips.begin() + toIndex, clip);

    int64_t position = 0;
    for (size_t i = 0; i < m_clips.size(); i++) {
        m_clips[i]->SetTimelinePosition(position);
        position += m_clips[i]->GetDuration();
    }

    RecalculateDuration();

    return 0;
}

int Timeline::SwapClips(int index1, int index2) {
    return MoveClip(index1, index2);
}

TimelineClip* Timeline::GetClip(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index < 0 || index >= static_cast<int>(m_clips.size())) {
        return nullptr;
    }
    
    return m_clips[index];
}

TimelineClip* Timeline::GetClipAtTime(int64_t timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LOGCATE("%s: Looking for clip at timestamp %lld, clip count=%zu", 
            TAG, (long long)timestamp, m_clips.size());
    
    TimelineClip* foundClip = nullptr;
    for (size_t i = 0; i < m_clips.size(); i++) {
        auto clip = m_clips[i];
        int64_t startTime = clip->GetTimelineStartTime();
        int64_t endTime = clip->GetTimelineEndTime();
        LOGCATE("%s: Clip[%zu] range: %lld - %lld (duration=%lld)", 
                TAG, i, (long long)startTime, (long long)endTime, (long long)(endTime - startTime));
        
        if (timestamp >= startTime && timestamp < endTime) {
            LOGCATE("%s: Found clip at timestamp %lld (clip[%zu])", TAG, (long long)timestamp, i);
            foundClip = clip;
            break;
        }
    }
    
    if (!foundClip) {
        LOGCATE("%s: No clip found at timestamp %lld", TAG, (long long)timestamp);
    }
    return foundClip;
}

int Timeline::GetClipIndex(TimelineClip* clip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (size_t i = 0; i < m_clips.size(); i++) {
        if (m_clips[i] == clip) {
            return static_cast<int>(i);
        }
    }
    
    return -1;
}

int64_t Timeline::GetDuration() const {
    return m_duration;
}

int64_t Timeline::GetFrameCount() const {
    if (m_config.frameRate <= 0) {
        return 0;
    }
    return (m_duration * m_config.frameRate) / AV_TIME_BASE;
}

int64_t Timeline::GetFrameTimestamp(int64_t frameIndex) const {
    if (m_config.frameRate <= 0) {
        return 0;
    }
    return (frameIndex * AV_TIME_BASE) / m_config.frameRate;
}

void Timeline::RecalculateDuration() {
    m_duration = 0;
    
    for (auto clip : m_clips) {
        int64_t endTime = clip->GetTimelineEndTime();
        if (endTime > m_duration) {
            m_duration = endTime;
        }
    }
}

int64_t Timeline::FindNextClipPosition() {
    if (m_clips.empty()) {
        return 0;
    }

    int64_t maxEnd = 0;
    for (auto clip : m_clips) {
        int64_t end = clip->GetTimelineEndTime();
        if (end > maxEnd) {
            maxEnd = end;
        }
    }
    
    return maxEnd;
}

void Timeline::PrintTimelineInfo() {
    LOGCATE("=== Timeline Info ===");
    LOGCATE("Width: %d, Height: %d, FPS: %d", 
            m_config.width, m_config.height, m_config.frameRate);
    LOGCATE("Total Duration: %lld us (%.2f s)", 
            (long long)m_duration, m_duration / 1000000.0f);
    LOGCATE("Clip Count: %zu", m_clips.size());
    
    for (size_t i = 0; i < m_clips.size(); i++) {
        TimelineClip* clip = m_clips[i];
        ClipInfo info = clip->GetClipInfo();
        LOGCATE("  Clip[%zu]: %s", i, info.sourcePath.c_str());
        LOGCATE("    Position: %lld - %lld", 
                (long long)clip->GetTimelineStartTime(),
                (long long)clip->GetTimelineEndTime());
        LOGCATE("    Size: %dx%d, Duration: %lld us", 
                info.width, info.height, (long long)info.duration);
    }
    LOGCATE("====================");
}
