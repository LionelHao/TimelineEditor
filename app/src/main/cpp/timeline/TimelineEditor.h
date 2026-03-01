#ifndef LEARNFFMPEG_TIMELINEEDITOR_H
#define LEARNFFMPEG_TIMELINEEDITOR_H

#include <jni.h>
#include <string>
#include <vector>
#include <mutex>
#include "Timeline.h"
#include "TimelineEncoder.h"
#include "VideoFilter.h"
#include "WatermarkFilter.h"
#include "HistogramEqualizationFilter.h"

extern "C" {
#include <GLES3/gl3.h>
}

enum class EditorState {
    EDITOR_STATE_IDLE,
    EDITOR_STATE_READY,
    EDITOR_STATE_PREVIEWING,
    EDITOR_STATE_EXPORTING,
    EDITOR_STATE_ERROR
};

typedef void (*EditorProgressCallback)(void* context, int progress, int total);
typedef void (*EditorCompleteCallback)(void* context, int result);

class TimelineEditor {
public:
    static TimelineEditor* GetInstance();
    static void ReleaseInstance();

    int Init(JNIEnv* env, jobject obj);
    void UnInit();

    int CreateTimeline(int width, int height, int frameRate);
    void DestroyTimeline();

    int AddVideoClip(const char* filePath);
    int AddVideoClipAtPosition(const char* filePath, int64_t positionMs);
    int RemoveVideoClip(int index);
    int MoveVideoClip(int fromIndex, int toIndex);

    int GetClipCount();
    int64_t GetTimelineDuration();
    int64_t GetCurrentPosition();
    void SetCurrentPosition(int64_t positionMs);

    int SetWatermark(const char* imagePath, int position, float opacity, float scale);
    int SetHistogramEqualization(bool enabled, float intensity);

    int SetOutputPath(const char* outputPath);
    int SetExportParams(int width, int height, int bitrate);
    int StartExport();
    void StopExport();
    void PauseExport();
    void ResumeExport();

    float GetExportProgress();
    bool IsExporting();

    int PreviewFrame(int64_t positionMs);
    int StartPreview();
    void StopPreview();

    EditorState GetState() const { return m_state; }
    Timeline* GetTimeline() { return m_timeline; }

    void SetProgressCallback(void* context, EditorProgressCallback callback);
    void SetCompleteCallback(void* context, EditorCompleteCallback callback);

    JNIEnv* GetJNIEnv(bool* isAttach);
    jobject GetJavaObj();
    JavaVM* GetJavaVM();

private:
    TimelineEditor();
    ~TimelineEditor();

    int InitFilters();
    void UnInitFilters();

    int ApplyFilters(NativeImage* input, NativeImage* output);

    static void OnEncodeProgress(void* context, int progress, int total);
    static void OnEncodeComplete(void* context, int result);

private:
    static TimelineEditor* s_Instance;
    static std::mutex s_Mutex;

    JavaVM* m_javaVM;
    jobject m_javaObj;

    Timeline* m_timeline;
    TimelineEncoder* m_encoder;

    WatermarkFilter* m_watermarkFilter;
    HistogramEqualizationFilter* m_histogramFilter;

    bool m_watermarkEnabled;
    bool m_histogramEnabled;
    std::string m_watermarkPath;

    int m_exportWidth;
    int m_exportHeight;
    int m_exportBitrate;
    bool m_exportParamsSet;

    EditorState m_state;
    int64_t m_currentPosition;

    std::mutex m_mutex;

    void* m_progressContext;
    EditorProgressCallback m_progressCallback;
    void* m_completeContext;
    EditorCompleteCallback m_completeCallback;
};

#endif
