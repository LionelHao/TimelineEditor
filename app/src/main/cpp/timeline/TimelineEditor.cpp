#include "TimelineEditor.h"
#include "LogUtil.h"

#define TAG "TimelineEditor"

TimelineEditor* TimelineEditor::s_Instance = nullptr;
std::mutex TimelineEditor::s_Mutex;

TimelineEditor::TimelineEditor()
    : m_javaVM(nullptr)
    , m_javaObj(nullptr)
    , m_timeline(nullptr)
    , m_encoder(nullptr)
    , m_watermarkFilter(nullptr)
    , m_histogramFilter(nullptr)
    , m_watermarkEnabled(false)
    , m_histogramEnabled(false)
    , m_exportWidth(1920)
    , m_exportHeight(1080)
    , m_exportBitrate(15000000)
    , m_exportParamsSet(false)
    , m_state(EditorState::EDITOR_STATE_IDLE)
    , m_currentPosition(0)
    , m_progressContext(nullptr)
    , m_progressCallback(nullptr)
    , m_completeContext(nullptr)
    , m_completeCallback(nullptr)
    , m_previewWindow(nullptr)
    , m_previewRenderer(nullptr)
    , m_glRender(nullptr)
    , m_previewThread(nullptr)
    , m_decodeThread(nullptr)
    , m_previewRunning(false)
    , m_previewPaused(false)
    , m_previewWidth(1920)
    , m_previewHeight(1080)
    , m_renderType(RENDER_TYPE_ANWINDOW)
    , m_surfaceWidth(0)
    , m_surfaceHeight(0)
    , m_swsContext(nullptr)
    , m_swsContextSrcWidth(0)
    , m_swsContextSrcHeight(0)
    , m_swsContextSrcFormat(AV_PIX_FMT_NONE)
    , m_swsContextDstWidth(0)
    , m_swsContextDstHeight(0)
    , m_rgbaFrame(nullptr)
    , m_rgbaBuffer(nullptr)
{
}

TimelineEditor::~TimelineEditor() {
    UnInit();
}

TimelineEditor* TimelineEditor::GetInstance() {
    if (s_Instance == nullptr) {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_Instance == nullptr) {
            s_Instance = new TimelineEditor();
        }
    }
    return s_Instance;
}

void TimelineEditor::ReleaseInstance() {
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (s_Instance) {
        delete s_Instance;
        s_Instance = nullptr;
    }
}

int TimelineEditor::Init(JNIEnv* env, jobject obj) {
    if (env->GetJavaVM(&m_javaVM) != JNI_OK) {
        LOGCATE("%s: GetJavaVM failed", TAG);
        return -1;
    }

    m_javaObj = env->NewGlobalRef(obj);
    if (!m_javaObj) {
        LOGCATE("%s: NewGlobalRef failed", TAG);
        return -1;
    }

    m_encoder = new TimelineEncoder();
    if (!m_encoder) {
        LOGCATE("%s: Create TimelineEncoder failed", TAG);
        return -1;
    }

    if (InitFilters() != 0) {
        LOGCATE("%s: InitFilters failed", TAG);
        return -1;
    }

    m_state = EditorState::EDITOR_STATE_READY;
    LOGCATE("%s: TimelineEditor initialized", TAG);

    return 0;
}

void TimelineEditor::UnInit() {
    StopPreview();
    
    DestroyTimeline();

    UnInitFilters();

    if (m_encoder) {
        delete m_encoder;
        m_encoder = nullptr;
    }

    if (m_javaObj && m_javaVM) {
        bool isAttach = false;
        JNIEnv* env = GetJNIEnv(&isAttach);
        if (env) {
            env->DeleteGlobalRef(m_javaObj);
        }
        if (isAttach && m_javaVM) {
            m_javaVM->DetachCurrentThread();
        }
    }

    m_javaObj = nullptr;
    m_javaVM = nullptr;
    m_state = EditorState::EDITOR_STATE_IDLE;
}

int TimelineEditor::InitFilters() {
    m_watermarkFilter = new WatermarkFilter();
    if (!m_watermarkFilter) {
        LOGCATE("%s: Create WatermarkFilter failed", TAG);
        return -1;
    }

    m_histogramFilter = new HistogramEqualizationFilter();
    if (!m_histogramFilter) {
        LOGCATE("%s: Create HistogramEqualizationFilter failed", TAG);
        delete m_watermarkFilter;
        m_watermarkFilter = nullptr;
        return -1;
    }

    return 0;
}

void TimelineEditor::UnInitFilters() {
    if (m_watermarkFilter) {
        m_watermarkFilter->UnInit();
        delete m_watermarkFilter;
        m_watermarkFilter = nullptr;
    }

    if (m_histogramFilter) {
        m_histogramFilter->UnInit();
        delete m_histogramFilter;
        m_histogramFilter = nullptr;
    }
}

int TimelineEditor::CreateTimeline(int width, int height, int frameRate) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_timeline) {
        delete m_timeline;
    }

    m_timeline = new Timeline();
    if (!m_timeline) {
        LOGCATE("%s: Create Timeline failed", TAG);
        return -1;
    }

    TimelineConfig config;
    config.width = width;
    config.height = height;
    config.frameRate = frameRate;
    config.bitRate = width * height * frameRate / 10;

    if (m_timeline->Init(config) != 0) {
        LOGCATE("%s: Timeline Init failed", TAG);
        delete m_timeline;
        m_timeline = nullptr;
        return -1;
    }

    m_currentPosition = 0;
    LOGCATE("%s: Timeline created, %dx%d @ %dfps", TAG, width, height, frameRate);

    return 0;
}

void TimelineEditor::DestroyTimeline() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_timeline) {
        m_timeline->UnInit();
        delete m_timeline;
        m_timeline = nullptr;
    }
}

int TimelineEditor::AddVideoClip(const char* filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_timeline || !filePath) {
        LOGCATE("%s: Invalid timeline or filePath", TAG);
        return -1;
    }

    int index = m_timeline->AddClip(filePath);
    if (index < 0) {
        LOGCATE("%s: AddClip failed", TAG);
        return -1;
    }

    LOGCATE("%s: Added clip at index %d: %s", TAG, index, filePath);
    return index;
}

int TimelineEditor::AddVideoClipAtPosition(const char* filePath, int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_timeline || !filePath) {
        LOGCATE("%s: Invalid timeline or filePath", TAG);
        return -1;
    }

    int64_t positionUs = positionMs * 1000;
    int index = m_timeline->AddClip(filePath, positionUs);
    if (index < 0) {
        LOGCATE("%s: AddClip failed", TAG);
        return -1;
    }

    return index;
}

int TimelineEditor::RemoveVideoClip(int index) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_timeline) {
        return -1;
    }

    return m_timeline->RemoveClip(index);
}

int TimelineEditor::MoveVideoClip(int fromIndex, int toIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_timeline) {
        return -1;
    }

    return m_timeline->MoveClip(fromIndex, toIndex);
}

int TimelineEditor::GetClipCount() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_timeline) {
        return 0;
    }

    return m_timeline->GetClipCount();
}

int64_t TimelineEditor::GetTimelineDuration() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_timeline) {
        return 0;
    }

    return m_timeline->GetDuration() / 1000;
}

int64_t TimelineEditor::GetCurrentPosition() {
    return m_currentPosition;
}

void TimelineEditor::SetCurrentPosition(int64_t positionMs) {
    m_currentPosition = positionMs;
}

int TimelineEditor::SetWatermark(const char* imagePath, int position, float opacity, float scale) {
    if (!m_watermarkFilter) {
        LOGCATE("%s: WatermarkFilter is null", TAG);
        return -1;
    }

    // 保存水印路径（即使 OpenGL 滤镜初始化失败，也要保存路径供 CPU 水印使用）
    if (imagePath) {
        m_watermarkPath = imagePath;
        LOGCATE("%s: Watermark path saved: %s", TAG, imagePath);
    }

    // 尝试初始化 OpenGL 滤镜（用于预览）
    if (m_watermarkFilter->Init() != 0) {
        LOGCATE("%s: WatermarkFilter Init failed (OpenGL filter not available, will use CPU watermark for export)", TAG);
        // 不返回错误，继续使用 CPU 水印
    } else {
        // OpenGL 滤镜初始化成功，加载水印
        int ret = m_watermarkFilter->SetWatermarkImage(imagePath);
        if (ret != 0) {
            LOGCATE("%s: SetWatermarkImage failed for path: %s", TAG, imagePath);
            // 不返回错误，继续使用 CPU 水印
        }
        
        m_watermarkFilter->SetPosition((WatermarkPosition)position);
        m_watermarkFilter->SetOpacity(opacity);
        m_watermarkFilter->SetScale(scale);
    }

    m_watermarkEnabled = true;

    LOGCATE("%s: Watermark set successfully (CPU watermark for export), position=%d, opacity=%.2f, scale=%.2f, path=%s", 
            TAG, position, opacity, scale, m_watermarkPath.c_str());

    return 0;
}

int TimelineEditor::SetHistogramEqualization(bool enabled, float intensity) {
    if (!m_histogramFilter) {
        return -1;
    }

    if (enabled && !m_histogramFilter->IsInitialized()) {
        if (m_histogramFilter->Init() != 0) {
            LOGCATE("%s: HistogramFilter Init failed", TAG);
            return -1;
        }
    }

    m_histogramFilter->SetIntensity(intensity);
    m_histogramEnabled = enabled;

    LOGCATE("%s: Histogram equalization %s, intensity=%.2f", 
            TAG, enabled ? "enabled" : "disabled", intensity);

    return 0;
}

int TimelineEditor::SetOutputPath(const char* outputPath) {
    if (!m_timeline || !outputPath) {
        return -1;
    }

    m_timeline->SetOutputPath(outputPath);
    return 0;
}

int TimelineEditor::SetExportParams(int width, int height, int bitrate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (width <= 0 || height <= 0 || bitrate <= 0) {
        LOGCATE("%s: Invalid export params: %dx%d, bitrate=%d", TAG, width, height, bitrate);
        return -1;
    }
    
    m_exportWidth = width;
    m_exportHeight = height;
    m_exportBitrate = bitrate;
    m_exportParamsSet = true;
    
    LOGCATE("%s: Export params set: %dx%d, bitrate=%d", TAG, width, height, bitrate);
    return 0;
}

int TimelineEditor::StartExport() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_timeline || !m_encoder) {
        LOGCATE("%s: Timeline or encoder not ready", TAG);
        return -1;
    }

    if (m_timeline->GetClipCount() == 0) {
        LOGCATE("%s: No clips in timeline", TAG);
        return -1;
    }

    const TimelineConfig& config = m_timeline->GetConfig();
    
    // 使用用户设置的导出参数或默认值
    int exportWidth = m_exportParamsSet ? m_exportWidth : config.width;
    int exportHeight = m_exportParamsSet ? m_exportHeight : config.height;
    int exportBitrate = m_exportParamsSet ? m_exportBitrate : config.bitRate;
    
    LOGCATE("%s: Starting export with params: %dx%d@%d, bitrate=%d", 
            TAG, exportWidth, exportHeight, config.frameRate, exportBitrate);
    
    if (m_encoder->Init(config.outputPath.c_str(), exportWidth, exportHeight, 
                        config.frameRate, exportBitrate) != 0) {
        LOGCATE("%s: Encoder Init failed", TAG);
        return -1;
    }

    // 设置水印到编码器
    if (m_watermarkEnabled && !m_watermarkPath.empty()) {
        // 位置由编码器根据视频尺寸自动计算 (右下角)
        m_encoder->SetWatermark(m_watermarkPath.c_str(), 0, 0, 0.8f, 0.2f);
        m_encoder->EnableWatermark(true);
        LOGCATE("%s: Watermark set to encoder: %s", TAG, m_watermarkPath.c_str());
    }

    m_encoder->SetProgressCallback(this, OnEncodeProgress);
    m_encoder->SetCompleteCallback(this, OnEncodeComplete);

    m_state = EditorState::EDITOR_STATE_EXPORTING;

    if (m_encoder->StartEncoding(m_timeline) != 0) {
        LOGCATE("%s: StartEncoding failed", TAG);
        m_state = EditorState::EDITOR_STATE_ERROR;
        return -1;
    }

    LOGCATE("%s: Export started", TAG);
    return 0;
}

void TimelineEditor::StopExport() {
    if (m_encoder) {
        m_encoder->StopEncoding();
    }
    m_state = EditorState::EDITOR_STATE_READY;
}

void TimelineEditor::PauseExport() {
    if (m_encoder) {
        m_encoder->PauseEncoding();
    }
}

void TimelineEditor::ResumeExport() {
    if (m_encoder) {
        m_encoder->ResumeEncoding();
    }
}

float TimelineEditor::GetExportProgress() {
    if (m_encoder) {
        return m_encoder->GetProgress();
    }
    return 0.0f;
}

bool TimelineEditor::IsExporting() {
    return m_state == EditorState::EDITOR_STATE_EXPORTING;
}

int TimelineEditor::ApplyFilters(NativeImage* input, NativeImage* output) {
    if (!input || !output) {
        return -1;
    }

    NativeImage tempImage = *input;

    if (m_histogramEnabled && m_histogramFilter) {
        m_histogramFilter->ApplyCPU(input);
    }

    if (m_watermarkEnabled && m_watermarkFilter) {
    }

    return 0;
}

void TimelineEditor::OnEncodeProgress(void* context, int progress, int total) {
    TimelineEditor* editor = static_cast<TimelineEditor*>(context);
    if (!editor) return;
    
    // Call Java callback via JNI
    bool isAttach = false;
    JNIEnv* env = editor->GetJNIEnv(&isAttach);
    if (env && editor->m_javaObj) {
        jclass clazz = env->GetObjectClass(editor->m_javaObj);
        if (clazz) {
            jmethodID methodId = env->GetMethodID(clazz, "onEditorEvent", "(IF)V");
            if (methodId) {
                float progressPercent = (total > 0) ? ((float)progress / total * 100.0f) : 0.0f;
                env->CallVoidMethod(editor->m_javaObj, methodId, 
                                   (jint)100, (jfloat)progressPercent);
            }
            env->DeleteLocalRef(clazz);
        }
    }
    if (isAttach && editor->m_javaVM) {
        editor->m_javaVM->DetachCurrentThread();
    }
    
    if (editor->m_progressCallback) {
        editor->m_progressCallback(editor->m_progressContext, progress, total);
    }
}

void TimelineEditor::OnEncodeComplete(void* context, int result) {
    TimelineEditor* editor = static_cast<TimelineEditor*>(context);
    if (!editor) return;
    
    editor->m_state = EditorState::EDITOR_STATE_READY;
    
    // Call Java callback via JNI
    bool isAttach = false;
    JNIEnv* env = editor->GetJNIEnv(&isAttach);
    if (env && editor->m_javaObj) {
        jclass clazz = env->GetObjectClass(editor->m_javaObj);
        if (clazz) {
            jmethodID methodId = env->GetMethodID(clazz, "onEditorEvent", "(IF)V");
            if (methodId) {
                int msgType = (result == 0) ? 101 : 102; // MSG_EXPORT_COMPLETE or MSG_EXPORT_ERROR
                env->CallVoidMethod(editor->m_javaObj, methodId, 
                                   (jint)msgType, (jfloat)result);
            }
            env->DeleteLocalRef(clazz);
        }
    }
    if (isAttach && editor->m_javaVM) {
        editor->m_javaVM->DetachCurrentThread();
    }
    
    if (editor->m_completeCallback) {
        editor->m_completeCallback(editor->m_completeContext, result);
    }
}

void TimelineEditor::SetProgressCallback(void* context, EditorProgressCallback callback) {
    m_progressContext = context;
    m_progressCallback = callback;
}

void TimelineEditor::SetCompleteCallback(void* context, EditorCompleteCallback callback) {
    m_completeContext = context;
    m_completeCallback = callback;
}

JNIEnv* TimelineEditor::GetJNIEnv(bool* isAttach) {
    *isAttach = false;
    JNIEnv* env = nullptr;

    if (!m_javaVM) {
        return nullptr;
    }

    int status = m_javaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status != JNI_OK) {
        status = m_javaVM->AttachCurrentThread(&env, nullptr);
        if (status != JNI_OK) {
            return nullptr;
        }
        *isAttach = true;
    }

    return env;
}

jobject TimelineEditor::GetJavaObj() {
    return m_javaObj;
}

JavaVM* TimelineEditor::GetJavaVM() {
    return m_javaVM;
}

int TimelineEditor::SetPreviewSurface(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_previewWindow) {
        ANativeWindow_release(m_previewWindow);
        m_previewWindow = nullptr;
    }
    
    m_previewWindow = window;
    
    if (m_previewWindow) {
        // 增加引用计数，防止Java层销毁Surface时指针悬空
        ANativeWindow_acquire(m_previewWindow);
        m_previewWidth = ANativeWindow_getWidth(m_previewWindow);
        m_previewHeight = ANativeWindow_getHeight(m_previewWindow);
        LOGCATE("%s: Preview surface set, size=%dx%d", TAG, m_previewWidth, m_previewHeight);
    }
    
    return 0;
}

int TimelineEditor::InitPreviewRenderer() {
    if (m_previewRenderer) {
        return 0;
    }
    
    if (!m_previewWindow) {
        LOGCATE("%s: Preview window not set", TAG);
        return -1;
    }
    
    if (!m_timeline) {
        LOGCATE("%s: Timeline not set", TAG);
        return -1;
    }
    
    m_previewRenderer = new NativeRender(m_previewWindow);
    if (!m_previewRenderer) {
        LOGCATE("%s: Failed to create preview renderer", TAG);
        return -1;
    }
    
    int videoWidth = m_timeline->GetWidth();
    int videoHeight = m_timeline->GetHeight();
    int dstSize[2] = {0};
    m_previewRenderer->Init(videoWidth, videoHeight, dstSize);
    
    m_previewWidth = dstSize[0] > 0 ? dstSize[0] : videoWidth;
    m_previewHeight = dstSize[1] > 0 ? dstSize[1] : videoHeight;
    
    if (m_rgbaBuffer) {
        av_free(m_rgbaBuffer);
        m_rgbaBuffer = nullptr;
    }
    if (m_rgbaFrame) {
        av_frame_free(&m_rgbaFrame);
        m_rgbaFrame = nullptr;
    }
    
    int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, m_previewWidth, m_previewHeight, 1);
    m_rgbaBuffer = (uint8_t*)av_malloc(bufferSize);
    if (!m_rgbaBuffer) {
        LOGCATE("%s: Failed to allocate RGBA buffer", TAG);
        return -1;
    }
    
    m_rgbaFrame = av_frame_alloc();
    if (!m_rgbaFrame) {
        LOGCATE("%s: Failed to allocate RGBA frame", TAG);
        av_free(m_rgbaBuffer);
        m_rgbaBuffer = nullptr;
        return -1;
    }
    
    av_image_fill_arrays(m_rgbaFrame->data, m_rgbaFrame->linesize, m_rgbaBuffer, 
                        AV_PIX_FMT_RGBA, m_previewWidth, m_previewHeight, 1);
    m_rgbaFrame->width = m_previewWidth;
    m_rgbaFrame->height = m_previewHeight;
    m_rgbaFrame->format = AV_PIX_FMT_RGBA;
    
    m_swsContextSrcWidth = 0;
    m_swsContextSrcHeight = 0;
    m_swsContextSrcFormat = AV_PIX_FMT_NONE;
    m_swsContextDstWidth = m_previewWidth;
    m_swsContextDstHeight = m_previewHeight;
    
    LOGCATE("%s: Preview renderer initialized with video size %dx%d, output size %dx%d", 
             TAG, videoWidth, videoHeight, m_previewWidth, m_previewHeight);
    return 0;
}

void TimelineEditor::UninitPreviewRenderer() {
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    if (m_rgbaFrame) {
        av_frame_free(&m_rgbaFrame);
        m_rgbaFrame = nullptr;
    }
    if (m_rgbaBuffer) {
        av_free(m_rgbaBuffer);
        m_rgbaBuffer = nullptr;
    }
    if (m_previewRenderer) {
        m_previewRenderer->UnInit();
        delete m_previewRenderer;
        m_previewRenderer = nullptr;
    }
}

int TimelineEditor::RenderPreviewFrame(int64_t positionMs) {
    if (!m_timeline) {
        LOGCATE("%s: Timeline not ready", TAG);
        return -1;
    }

    int64_t positionUs = positionMs * 1000;

    TimelineClip* clip = m_timeline->GetClipAtTime(positionUs);
    if (!clip) {
        LOGCATE("%s: No clip found at time %ld", TAG, positionUs);
        return -1;
    }

    int64_t clipStartTime = clip->GetTimelineStartTime();
    int64_t clipLocalTime = positionUs - clipStartTime;

    if (clip->SeekToTime(clipLocalTime) != 0) {
        LOGCATE("%s: Failed to seek to time %ld", TAG, clipLocalTime);
        return -1;
    }

    AVFrame* frame = nullptr;
    int ret = clip->DecodeFrame(&frame);
    if (ret != 0 || !frame) {
        LOGCATE("%s: Failed to decode frame, ret=%d", TAG, ret);
        return -1;
    }

    if (frame->width <= 0 || frame->height <= 0) {
        LOGCATE("%s: Invalid frame size: %dx%d", TAG, frame->width, frame->height);
        return -1;
    }

    if (frame->format < 0) {
        LOGCATE("%s: Invalid frame format: %d", TAG, frame->format);
        return -1;
    }

    LOGCATE("%s: Decoded frame at position %ldms, frame pts=%ld, frame=%p", TAG, positionMs, frame->pts, frame);

    if (m_renderType == RENDER_TYPE_OPENGL && m_glRender) {
        LOGCATE("%s: OpenGL rendering path, calling RenderFrameToWindow, m_glRender=%p", TAG, m_glRender);
        int renderResult = m_glRender->RenderFrameToWindow(frame);
        if (renderResult == 0) {
            LOGCATE("%s: Rendered frame via OpenGL at position %ldms", TAG, positionMs);
        } else {
            LOGCATE("%s: OpenGL rendering failed with result=%d", TAG, renderResult);
        }
    } else if (m_renderType == RENDER_TYPE_OPENGL && !m_glRender) {
        LOGCATE("%s: OpenGL rendering path, but m_glRender is null!", TAG);
    } else if (m_renderType == RENDER_TYPE_ANWINDOW && m_previewWindow && m_previewRenderer) {
        int srcWidth = frame->width;
        int srcHeight = frame->height;
        AVPixelFormat srcFormat = (AVPixelFormat)frame->format;
        int dstWidth = m_previewWidth;
        int dstHeight = m_previewHeight;

        if (!m_rgbaFrame || !m_rgbaBuffer) {
            LOGCATE("%s: RGBA frame not initialized", TAG);
            return -1;
        }

        if (!m_swsContext || m_swsContextSrcWidth != srcWidth ||
            m_swsContextSrcHeight != srcHeight || m_swsContextSrcFormat != srcFormat) {
            if (m_swsContext) {
                sws_freeContext(m_swsContext);
                m_swsContext = nullptr;
            }

            m_swsContext = sws_getContext(srcWidth, srcHeight, srcFormat,
                                         dstWidth, dstHeight, AV_PIX_FMT_RGBA,
                                         SWS_FAST_BILINEAR, NULL, NULL, NULL);
            if (!m_swsContext) {
                LOGCATE("%s: Failed to create sws context", TAG);
                return -1;
            }

            m_swsContextSrcWidth = srcWidth;
            m_swsContextSrcHeight = srcHeight;
            m_swsContextSrcFormat = srcFormat;
        }

        sws_scale(m_swsContext, frame->data, frame->linesize, 0, srcHeight,
                  m_rgbaFrame->data, m_rgbaFrame->linesize);

        m_previewRenderer->RenderVideoFrame(m_rgbaFrame);
        LOGCATE("%s: Rendered frame via ANativeWindow at position %ldms", TAG, positionMs);
    }

    return 0;
}

void TimelineEditor::PreviewThreadFunc() {
    LOGCATE("%s: Preview thread started", TAG);
    
    int64_t duration = m_timeline ? m_timeline->GetDuration() / 1000 : 0;
    int frameRate = m_timeline ? m_timeline->GetFrameRate() : 30;
    int64_t frameInterval = 1000 / (frameRate > 0 ? frameRate : 30);
    
    while (m_previewRunning) {
        if (m_previewPaused) {
            std::unique_lock<std::mutex> lock(m_previewMutex);
            m_previewCV.wait(lock, [this] { 
                return !m_previewPaused || !m_previewRunning; 
            });
            if (!m_previewRunning) break;
            continue;
        }
        
        if (m_timeline) {
            m_currentPosition += frameInterval;
            if (m_currentPosition >= duration) {
                m_currentPosition = 0;
            }
            
            NotifyPositionChanged(m_currentPosition);
            
            // 对于 OpenGL 渲染，设置需要渲染的标志，让 GLSurfaceView 的渲染线程处理
            if (m_renderType == RENDER_TYPE_OPENGL) {
                m_needRender = true;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(frameInterval));
    }
    
    LOGCATE("%s: Preview thread stopped", TAG);
}

void TimelineEditor::NotifyPositionChanged(int64_t positionMs) {
    LOGCATE("%s: NotifyPositionChanged called, position=%ld", TAG, positionMs);
    bool isAttach = false;
    JNIEnv* env = GetJNIEnv(&isAttach);
    if (env && m_javaObj) {
        jclass clazz = env->GetObjectClass(m_javaObj);
        if (clazz) {
            jmethodID methodId = env->GetMethodID(clazz, "onEditorEvent", "(IF)V");
            if (methodId) {
                LOGCATE("%s: Calling onEditorEvent with MSG_PREVIEW_POSITION, position=%ld", TAG, positionMs);
                env->CallVoidMethod(m_javaObj, methodId, 
                                   (jint)MSG_PREVIEW_POSITION, (jfloat)positionMs);
            } else {
                LOGCATE("%s: Failed to get methodId for onEditorEvent", TAG);
            }
            env->DeleteLocalRef(clazz);
        } else {
            LOGCATE("%s: Failed to get class for m_javaObj", TAG);
        }
    } else {
        LOGCATE("%s: env=%p, m_javaObj=%p", TAG, env, m_javaObj);
    }
    if (isAttach && m_javaVM) {
        m_javaVM->DetachCurrentThread();
    }
}

int TimelineEditor::PreviewFrame(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_timeline) {
        LOGCATE("%s: Timeline not ready", TAG);
        return -1;
    }
    
    m_currentPosition = positionMs;
    return RenderPreviewFrame(positionMs);
}

int TimelineEditor::StartPreview() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_previewRunning) {
        LOGCATE("%s: Preview already running", TAG);
        return 0;
    }
    
    if (!m_timeline) {
        LOGCATE("%s: No timeline to preview", TAG);
        return -1;
    }
    
    if (m_renderType == RENDER_TYPE_ANWINDOW && !m_previewWindow) {
        LOGCATE("%s: No preview surface set", TAG);
        return -1;
    }
    
    if (m_renderType == RENDER_TYPE_ANWINDOW) {
        // 初始化预览渲染器
        if (!m_previewRenderer) {
            if (InitPreviewRenderer() != 0) {
                LOGCATE("%s: Failed to init preview renderer", TAG);
                return -1;
            }
        }
    }
    
    m_previewRunning = true;
    m_previewPaused = false;
    m_needRender = false;
    m_currentPosition = 0;
    m_state = EditorState::EDITOR_STATE_PREVIEWING;
    
    m_previewThread = new std::thread(&TimelineEditor::PreviewThreadFunc, this);
    if (!m_previewThread) {
        m_previewRunning = false;
        m_state = EditorState::EDITOR_STATE_READY;
        return -1;
    }
    
    LOGCATE("%s: Preview started", TAG);
    return 0;
}

void TimelineEditor::StopPreview() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_previewRunning) {
            return;
        }
        
        m_previewRunning = false;
        m_previewPaused = false;
    }
    
    m_previewCV.notify_all();
    
    if (m_previewThread && m_previewThread->joinable()) {
        m_previewThread->join();
        delete m_previewThread;
        m_previewThread = nullptr;
    }
    
    // 清理预览渲染器
    if (m_previewRenderer) {
        delete m_previewRenderer;
        m_previewRenderer = nullptr;
    }
    
    // 释放预览窗口（仅在ANativeWindow渲染时）
    if (m_renderType == RENDER_TYPE_ANWINDOW && m_previewWindow) {
        ANativeWindow_release(m_previewWindow);
        m_previewWindow = nullptr;
    }
    
    m_state = EditorState::EDITOR_STATE_READY;
    LOGCATE("%s: Preview stopped", TAG);
}

bool TimelineEditor::IsPreviewing() {
    return m_previewRunning && !m_previewPaused;
}

void TimelineEditor::PausePreview() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_previewRunning && !m_previewPaused) {
        m_previewPaused = true;
        LOGCATE("%s: Preview paused", TAG);
    }
}

void TimelineEditor::ResumePreview() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_previewRunning && m_previewPaused) {
        m_previewPaused = false;
        m_previewCV.notify_all();
        LOGCATE("%s: Preview resumed", TAG);
    }
}

bool TimelineEditor::IsPreviewPaused() {
    return m_previewPaused;
}

int TimelineEditor::SeekToPosition(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentPosition = positionMs;
    LOGCATE("%s: Seek to position %ldms", TAG, positionMs);
    return 0;
}

int TimelineEditor::OnSurfaceCreated(int renderType) {
    LOGCATE("%s: OnSurfaceCreated, renderType=%d", TAG, renderType);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_renderType = renderType;
    if (m_renderType == RENDER_TYPE_OPENGL) {
        if (!m_glRender) {
            m_glRender = new TimelineGLRender();
        }
        if (m_glRender) {
            // 尝试获取 GLSurfaceView 的 EGL 上下文
            EGLDisplay eglDisplay = eglGetCurrentDisplay();
            EGLContext eglContext = eglGetCurrentContext();
            
            if (eglDisplay != EGL_NO_DISPLAY && eglContext != EGL_NO_CONTEXT) {
                LOGCATE("%s: Using GLSurfaceView EGL context: display=%p, context=%p", 
                        TAG, eglDisplay, eglContext);
                if (m_glRender->InitWithEGLContext(eglContext, eglDisplay) != 0) {
                    LOGCATE("%s: InitWithEGLContext failed", TAG);
                }
            } else {
                LOGCATE("%s: No current EGL context found, creating new one", TAG);
                m_glRender->Init();
            }
        }
    }
    return 0;
}

int TimelineEditor::OnSurfaceChanged(int renderType, int width, int height) {
    LOGCATE("%s: OnSurfaceChanged, renderType=%d, size=%dx%d", TAG, renderType, width, height);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_surfaceWidth = width;
    m_surfaceHeight = height;
    if (m_renderType == RENDER_TYPE_OPENGL && m_glRender) {
        m_glRender->SetSurfaceSize(width, height);
    }
    return 0;
}

int TimelineEditor::OnDrawFrame(int renderType) {
    LOGCATE("%s: OnDrawFrame called, renderType=%d, m_glRender=%p", TAG, renderType, m_glRender);
    
    if (renderType != RENDER_TYPE_OPENGL) {
        LOGCATE("%s: OnDrawFrame - renderType is not OPENGL, returning", TAG);
        return 0;
    }

    if (!m_glRender) {
        LOGCATE("%s: OnDrawFrame - m_glRender is null, creating new TimelineGLRender", TAG);
        m_glRender = new TimelineGLRender();
        if (m_glRender) {
            m_glRender->Init();
        }
        LOGCATE("%s: OnDrawFrame - m_glRender created: %p", TAG, m_glRender);
    }
    
    if (!m_timeline || m_timeline->GetClipCount() == 0) {
        LOGCATE("%s: OnDrawFrame - conditions not met: m_glRender=%p, m_timeline=%p, clipCount=%d", 
                TAG, m_glRender, m_timeline, m_timeline ? m_timeline->GetClipCount() : 0);
        return 0;
    }

    bool previewRunning = m_previewRunning.load();
    bool previewPaused = m_previewPaused.load();
    LOGCATE("%s: OnDrawFrame - m_previewRunning=%d, m_previewPaused=%d, m_currentPosition=%ld", 
            TAG, previewRunning, previewPaused, m_currentPosition);

    if (previewRunning && !previewPaused) {
        LOGCATE("%s: OnDrawFrame - calling RenderPreviewFrame", TAG);
        int renderResult = RenderPreviewFrame(m_currentPosition);
        if (renderResult != 0) {
            LOGCATE("%s: OnDrawFrame - RenderPreviewFrame failed with result=%d", TAG, renderResult);
        }
    } else {
        LOGCATE("%s: OnDrawFrame - preview not running or paused, skipping RenderPreviewFrame", TAG);
    }

    return 0;
}

bool TimelineEditor::CheckAndClearNeedRender() {
    if (m_needRender) {
        m_needRender = false;
        return true;
    }
    return false;
}
