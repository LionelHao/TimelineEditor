#ifndef LEARNFFMPEG_TIMELINEEDITOR_H
#define LEARNFFMPEG_TIMELINEEDITOR_H

#include <jni.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "Timeline.h"
#include "TimelineEncoder.h"
#include "TimelineGLRender.h"
#include "VideoFilter.h"
#include "WatermarkFilter.h"
#include "HistogramEqualizationFilter.h"
#include "NativeRender.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
}

enum class EditorState {
    EDITOR_STATE_IDLE,
    EDITOR_STATE_READY,
    EDITOR_STATE_PREVIEWING,
    EDITOR_STATE_EXPORTING,
    EDITOR_STATE_ERROR
};

constexpr int MSG_EXPORT_PROGRESS = 100;
constexpr int MSG_EXPORT_COMPLETE = 101;
constexpr int MSG_EXPORT_ERROR = 102;
constexpr int MSG_PREVIEW_FRAME = 200;
constexpr int MSG_PREVIEW_POSITION = 201;

constexpr int RENDER_TYPE_OPENGL = 0;
constexpr int RENDER_TYPE_ANWINDOW = 1;

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

    int SetPreviewSurface(ANativeWindow* window);
    int PreviewFrame(int64_t positionMs);
    int StartPreview();
    void StopPreview();
    void PausePreview();
    void ResumePreview();
    bool IsPreviewing();
    bool IsPreviewPaused();

    int SeekToPosition(int64_t positionMs);

    int OnSurfaceCreated(int renderType);
    int OnSurfaceChanged(int renderType, int width, int height);
    int OnDrawFrame(int renderType);
    bool CheckAndClearNeedRender();

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

    int InitPreviewRenderer();
    void UninitPreviewRenderer();
    void PreviewThreadFunc();
    void DecodeThreadFunc();
    int RenderPreviewFrame(int64_t positionMs);
    void NotifyPositionChanged(int64_t positionMs);

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

    ANativeWindow* m_previewWindow;
    NativeRender* m_previewRenderer;
    TimelineGLRender* m_glRender;
    std::thread* m_previewThread;
    std::thread* m_decodeThread;
    std::atomic<bool> m_previewRunning;
    std::atomic<bool> m_previewPaused;
    std::atomic<bool> m_needRender;
    std::condition_variable m_previewCV;
    std::mutex m_previewMutex;
    int m_previewWidth;
    int m_previewHeight;
    int m_renderType;
    int m_surfaceWidth;
    int m_surfaceHeight;

    struct SwsContext* m_swsContext;
    int m_swsContextSrcWidth;
    int m_swsContextSrcHeight;
    AVPixelFormat m_swsContextSrcFormat;
    int m_swsContextDstWidth;
    int m_swsContextDstHeight;

    AVFrame* m_rgbaFrame;
    uint8_t* m_rgbaBuffer;
};

#endif
