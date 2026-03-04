#include "TimelineEncoder.h"
#include "LogUtil.h"
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#define TAG "TimelineEncoder"
#define TIMEOUT_US 100000

TimelineEncoder::TimelineEncoder()
    : m_state(EncoderState::ENCODER_STATE_IDLE)
    , m_videoCodec(nullptr)
    , m_mediaMuxer(nullptr)
    , m_outputFormat(nullptr)
    , m_videoTrackIndex(-1)
    , m_frameIndex(0)
    , m_encodedFrameCount(0)
    , m_totalFrameCount(0)
    , m_swsContext(nullptr)
    , m_yuvFrame(nullptr)
    , m_nv12Buffer(nullptr)
    , m_encodeThread(nullptr)
    , m_isEncoding(false)
    , m_isPaused(false)
    , m_stopRequested(false)
    , m_progressContext(nullptr)
    , m_progressCallback(nullptr)
    , m_completeContext(nullptr)
    , m_completeCallback(nullptr)
    , m_filterContext(nullptr)
    , m_filterCallback(nullptr)
    , m_currentTimeline(nullptr)
    , m_glRender(nullptr)
    , m_useOpenGL(false)
    , m_pendingWatermarkOpacity(0.0f)
    , m_pendingWatermarkScale(0.0f)
    , m_pendingWatermarkOffsetX(0.0f)
    , m_pendingWatermarkOffsetY(0.0f)
    , m_pendingWatermarkSet(false)
    , m_cpuWatermark(nullptr)
    , m_watermarkEnabled(false)
    , m_lastProcessedClip(nullptr)
    , m_lastProcessedTime(-1)
    , m_lastProcessedClipIndex(-1)
    , m_lastFrameCache(nullptr)
    , m_eofFrameCache(nullptr)
    , m_lastPresentationPts(-1)
{
}

TimelineEncoder::~TimelineEncoder() {
    UnInit();
}

int TimelineEncoder::Init(const char* outputPath, int width, int height, int frameRate, int64_t bitRate) {
    LOGCATE("%s: Init called with bitrate=%lld", TAG, (long long)bitRate);
    
    EncoderConfig config;
    config.outputPath = outputPath;
    config.width = width;
    config.height = height;
    config.frameRate = frameRate;
    config.bitRate = bitRate;
    
    return Init(config);
}

int TimelineEncoder::Init(const EncoderConfig& config) {
    UnInit();
    
    m_config = config;
    
    LOGCATE("%s: Encoder config: %dx%d @ %dfps, bitrate=%lld", 
            TAG, m_config.width, m_config.height, m_config.frameRate, 
            (long long)m_config.bitRate);
    
    if (CreateEncoder() != 0) {
        LOGCATE("%s: CreateEncoder failed", TAG);
        return -1;
    }
    
    if (CreateMuxer() != 0) {
        LOGCATE("%s: CreateMuxer failed", TAG);
        DestroyEncoder();
        return -1;
    }
    
    // 创建 OpenGL 渲染器对象，但不在这里初始化
    // OpenGL 上下文需要在编码线程中初始化
    m_glRender = new TimelineGLRender();
    m_useOpenGL = true;  // 重新启用 OpenGL
    
    m_state = EncoderState::ENCODER_STATE_READY;
    LOGCATE("%s: TimelineEncoder initialized, %dx%d @ %dfps, bitrate=%lld",
            TAG, m_config.width, m_config.height, m_config.frameRate, 
            (long long)m_config.bitRate);
    
    return 0;
}

void TimelineEncoder::UnInit() {
    StopEncoding();
    DestroyMuxer();
    DestroyEncoder();
    
    // 销毁 OpenGL 渲染器
    if (m_glRender) {
        m_glRender->Uninit();
        delete m_glRender;
        m_glRender = nullptr;
    }
    
    if (m_cpuWatermark) {
        delete m_cpuWatermark;
        m_cpuWatermark = nullptr;
    }
    
    // 释放帧缓存
    if (m_lastFrameCache) {
        av_frame_free(&m_lastFrameCache);
        m_lastFrameCache = nullptr;
    }
    if (m_eofFrameCache) {
        av_frame_free(&m_eofFrameCache);
        m_eofFrameCache = nullptr;
    }
    
    m_state = EncoderState::ENCODER_STATE_IDLE;
}

int TimelineEncoder::CreateEncoder() {
    const char* mime = "video/avc";
    
    m_videoCodec = AMediaCodec_createEncoderByType(mime);
    if (!m_videoCodec) {
        LOGCATE("%s: AMediaCodec_createEncoderByType failed", TAG);
        return -1;
    }
    
    return ConfigureEncoder();
}

void TimelineEncoder::DestroyEncoder() {
    if (m_videoCodec) {
        AMediaCodec_stop(m_videoCodec);
        AMediaCodec_delete(m_videoCodec);
        m_videoCodec = nullptr;
    }
    
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    
    if (m_yuvFrame) {
        av_frame_free(&m_yuvFrame);
        m_yuvFrame = nullptr;
    }
    
    if (m_nv12Buffer) {
        free(m_nv12Buffer);
        m_nv12Buffer = nullptr;
    }
}

int TimelineEncoder::CreateMuxer() {
    int fd = open(m_config.outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LOGCATE("%s: Failed to open output file: %s, errno=%d", TAG, m_config.outputPath.c_str(), errno);
        return -1;
    }
    
    m_mediaMuxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!m_mediaMuxer) {
        LOGCATE("%s: AMediaMuxer_new failed", TAG);
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

void TimelineEncoder::DestroyMuxer() {
    if (m_mediaMuxer) {
        AMediaMuxer_delete(m_mediaMuxer);
        m_mediaMuxer = nullptr;
    }
    
    if (m_outputFormat) {
        AMediaFormat_delete(m_outputFormat);
        m_outputFormat = nullptr;
    }
}

int TimelineEncoder::ConfigureEncoder() {
    m_outputFormat = AMediaFormat_new();
    if (!m_outputFormat) {
        LOGCATE("%s: AMediaFormat_new failed", TAG);
        return -1;
    }
    
    AMediaFormat_setString(m_outputFormat, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(m_outputFormat, AMEDIAFORMAT_KEY_WIDTH, m_config.width);
    AMediaFormat_setInt32(m_outputFormat, AMEDIAFORMAT_KEY_HEIGHT, m_config.height);
    AMediaFormat_setInt32(m_outputFormat, AMEDIAFORMAT_KEY_COLOR_FORMAT, m_config.colorFormat);
    AMediaFormat_setInt32(m_outputFormat, AMEDIAFORMAT_KEY_BIT_RATE, (int32_t)m_config.bitRate);
    AMediaFormat_setInt32(m_outputFormat, AMEDIAFORMAT_KEY_FRAME_RATE, m_config.frameRate);
    AMediaFormat_setInt32(m_outputFormat, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, m_config.iframeInterval);
    
    LOGCATE("%s: ConfigureEncoder: width=%d, height=%d, bitrate=%d, framerate=%d", 
            TAG, m_config.width, m_config.height, (int32_t)m_config.bitRate, m_config.frameRate);
    
    media_status_t status = AMediaCodec_configure(m_videoCodec, m_outputFormat, nullptr, nullptr, 
                                                   AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status != AMEDIA_OK) {
        LOGCATE("%s: AMediaCodec_configure failed, status=%d", TAG, status);
        return -1;
    }
    
    return 0;
}

int TimelineEncoder::StartEncoder() {
    media_status_t status = AMediaCodec_start(m_videoCodec);
    if (status != AMEDIA_OK) {
        LOGCATE("%s: AMediaCodec_start failed, status=%d", TAG, status);
        return -1;
    }
    
    return 0;
}

int TimelineEncoder::StartEncoding(Timeline* timeline) {
    if (!timeline || timeline->GetClipCount() == 0) {
        LOGCATE("%s: Invalid timeline or no clips", TAG);
        return -1;
    }
    
    if (m_state != EncoderState::ENCODER_STATE_READY) {
        LOGCATE("%s: Encoder not ready, state=%d", TAG, m_state);
        return -1;
    }
    
    m_currentTimeline = timeline;
    m_totalFrameCount = timeline->GetFrameCount();
    m_encodedFrameCount = 0;
    m_frameIndex = 0;
    m_videoTrackIndex = -1;
    m_isEncoding = true;
    m_isPaused = false;
    m_stopRequested = false;
    
    // 重置编码状态跟踪变量（关键：确保每次导出都是干净的状态）
    m_lastProcessedClip = nullptr;
    m_lastProcessedTime = -1;
    m_lastProcessedClipIndex = -1;
    m_lastPresentationPts = -1;
    
    // 清空性能数据
    m_framePerformanceData.clear();
    clock_gettime(CLOCK_MONOTONIC, &m_exportStartTime);
    
    // 分配帧缓存（如果还没有分配）
    if (!m_lastFrameCache) {
        m_lastFrameCache = av_frame_alloc();
    }
    if (!m_eofFrameCache) {
        m_eofFrameCache = av_frame_alloc();
    }
    
    m_state = EncoderState::ENCODER_STATE_ENCODING;
    
    LOGCATE("%s: Timeline clip count=%d, duration=%lld, total frames=%lld", 
            TAG, timeline->GetClipCount(), (long long)timeline->GetDuration(), (long long)m_totalFrameCount);
    timeline->PrintTimelineInfo();
    
    m_encodeThread = new std::thread(EncodeThreadFunc, this);
    
    LOGCATE("%s: Start encoding, total frames=%lld", TAG, (long long)m_totalFrameCount);
    
    return 0;
}

void TimelineEncoder::StopEncoding() {
    m_stopRequested = true;
    m_isEncoding = false;
    m_condVar.notify_all();
    
    if (m_encodeThread && m_encodeThread->joinable()) {
        m_encodeThread->join();
        delete m_encodeThread;
        m_encodeThread = nullptr;
    }
    
    m_state = EncoderState::ENCODER_STATE_READY;
}

void TimelineEncoder::PauseEncoding() {
    m_isPaused = true;
}

void TimelineEncoder::ResumeEncoding() {
    m_isPaused = false;
    m_condVar.notify_all();
}

float TimelineEncoder::GetProgress() const {
    if (m_totalFrameCount == 0) return 0.0f;
    return (float)m_encodedFrameCount / m_totalFrameCount * 100.0f;
}

void TimelineEncoder::EncodeThreadFunc(TimelineEncoder* encoder) {
    LOGCATE("%s: EncodeThread started", TAG);
    
    // 在编码线程中初始化 OpenGL 上下文
    if (encoder->m_useOpenGL && encoder->m_glRender) {
        if (encoder->m_glRender->Init() == 0) {
            LOGCATE("%s: OpenGL renderer initialized in encode thread", TAG);
            
            // 检查是否有待设置的水印信息
            if (encoder->m_pendingWatermarkSet) {
                int ret = encoder->m_glRender->SetWatermark(
                    encoder->m_pendingWatermarkPath.c_str(),
                    encoder->m_pendingWatermarkOpacity,
                    encoder->m_pendingWatermarkScale,
                    encoder->m_pendingWatermarkOffsetX,
                    encoder->m_pendingWatermarkOffsetY
                );
                if (ret == 0) {
                    LOGCATE("%s: Applied pending OpenGL watermark", TAG);
                } else {
                    LOGCATE("%s: Failed to apply pending OpenGL watermark", TAG);
                }
            }
        } else {
            LOGCATE("%s: OpenGL renderer init failed, will use CPU fallback", TAG);
            encoder->m_useOpenGL = false;
        }
    }
    
    // 打印所有片段信息
    LOGCATE("%s: === Timeline Clips ===", TAG);
    for (int i = 0; i < encoder->m_currentTimeline->GetClipCount(); i++) {
        TimelineClip* clip = encoder->m_currentTimeline->GetClip(i);
        if (clip) {
            LOGCATE("%s: Clip[%d]: timeline=[%lld, %lld), source=[%lld, %lld), duration=%lld", 
                    TAG, i, 
                    (long long)clip->GetTimelineStartTime(), 
                    (long long)clip->GetTimelineEndTime(),
                    (long long)clip->GetClipInfo().sourceStartTime, 
                    (long long)clip->GetClipInfo().sourceEndTime,
                    (long long)clip->GetDuration());
        }
    }
    LOGCATE("%s: =======================", TAG);
    
    if (encoder->StartEncoder() != 0) {
        LOGCATE("%s: StartEncoder failed", TAG);
        encoder->m_state = EncoderState::ENCODER_STATE_ERROR;
        if (encoder->m_completeCallback) {
            encoder->m_completeCallback(encoder->m_completeContext, -1);
        }
        return;
    }
    
    int result = 0;
    int64_t totalFrames = encoder->m_totalFrameCount;
    
    // 获取时间线第一个片段的开始时间
    int64_t firstClipStartTime = 0;
    if (encoder->m_currentTimeline->GetClipCount() > 0) {
        TimelineClip* firstClip = encoder->m_currentTimeline->GetClip(0);
        if (firstClip) {
            firstClipStartTime = firstClip->GetTimelineStartTime();
        }
    }
    
    // 计算起始帧索引（跳过时间线开始前的空白）
    // 向上取整，确保时间戳 >= 片段开始时间
    int64_t startFrameIdx = 0;
    if (firstClipStartTime > 0 && encoder->m_currentTimeline->GetConfig().frameRate > 0) {
        startFrameIdx = (firstClipStartTime * encoder->m_currentTimeline->GetConfig().frameRate + AV_TIME_BASE - 1) / AV_TIME_BASE;
    }
    
    LOGCATE("%s: Starting encode, total frames=%lld, duration=%lld us, fps=%d, firstClipStart=%lld, startFrameIdx=%lld", 
            TAG, (long long)totalFrames, (long long)encoder->m_currentTimeline->GetDuration(), 
            encoder->m_currentTimeline->GetConfig().frameRate, (long long)firstClipStartTime, (long long)startFrameIdx);
    
    for (int64_t frameIdx = startFrameIdx; frameIdx < totalFrames && !encoder->m_stopRequested; frameIdx++) {
        while (encoder->m_isPaused && !encoder->m_stopRequested) {
            std::unique_lock<std::mutex> lock(encoder->m_mutex);
            encoder->m_condVar.wait(lock);
        }
        
        if (encoder->m_stopRequested) {
            break;
        }
        
        result = encoder->ProcessTimelineFrame(encoder->m_currentTimeline, frameIdx, startFrameIdx);
        if (result != 0) {
            LOGCATE("%s: ProcessTimelineFrame failed at frame %lld, stopping encode", TAG, (long long)frameIdx);
            encoder->m_stopRequested = true;  // 设置停止标志，防止后续写入
            break;
        }
        
        encoder->m_encodedFrameCount++;
        
        if (encoder->m_progressCallback) {
            // 进度相对于实际编码的帧数（从0开始）
            int progressFrames = (int)(frameIdx - startFrameIdx);
            int totalEncodeFrames = (int)(totalFrames - startFrameIdx);
            if (totalEncodeFrames <= 0) totalEncodeFrames = 1;
            encoder->m_progressCallback(encoder->m_progressContext, 
                                        progressFrames, totalEncodeFrames);
        }
    }
    
    // 只有在没有错误的情况下才正常结束
    if (result == 0) {
        encoder->DrainEncoder(true);
        if (encoder->m_mediaMuxer) {
            AMediaMuxer_stop(encoder->m_mediaMuxer);
        }
    }
    
    // 记录导出结束时间并保存性能数据
    clock_gettime(CLOCK_MONOTONIC, &encoder->m_exportEndTime);
    encoder->SavePerformanceData();
    
    encoder->m_state = EncoderState::ENCODER_STATE_READY;
    encoder->m_isEncoding = false;
    
    if (encoder->m_completeCallback) {
        encoder->m_completeCallback(encoder->m_completeContext, result);
    }
    
    LOGCATE("%s: EncodeThread finished, encoded %lld frames", 
            TAG, (long long)encoder->m_encodedFrameCount);
}

int TimelineEncoder::ProcessTimelineFrame(Timeline* timeline, int64_t frameIndex, int64_t startFrameIndex) {
    // 使用成员变量缓存最后一帧用于 EOF 时重复
    
    int64_t timestamp = timeline->GetFrameTimestamp(frameIndex);
    
    TimelineClip* clip = timeline->GetClipAtTime(timestamp);
    if (!clip) {
        // 检查是否在片段之间的空隙中，如果是则跳过此帧
        int64_t nextClipStart = -1;
        for (int i = 0; i < timeline->GetClipCount(); i++) {
            TimelineClip* c = timeline->GetClip(i);
            if (c && c->GetTimelineStartTime() > timestamp) {
                nextClipStart = c->GetTimelineStartTime();
                break;
            }
        }
        
        if (nextClipStart > 0) {
            // 在空隙中，跳过此帧但不报错
            LOGCATE("%s: Gap between clips at timestamp %lld, next clip starts at %lld, skipping frame", 
                    TAG, (long long)timestamp, (long long)nextClipStart);
            return 0; // 返回0表示成功，但跳过编码
        }
        
        LOGCATE("%s: No clip at timestamp %lld", TAG, (long long)timestamp);
        return -1;
    }
    
    int64_t timelineStart = clip->GetTimelineStartTime();
    int64_t sourceStart = clip->GetClipInfo().sourceStartTime;
    
    // 计算 clip 内的时间偏移（微秒）
    int64_t clipTimeOffset = timestamp - timelineStart;
    
    // 根据原始视频帧率和目标帧率转换时间
    // 如果原始视频帧率与目标帧率不同，需要转换
    int originalFPS = clip->GetClipInfo().frameRate;
    int targetFPS = timeline->GetConfig().frameRate;
    
    // 计算在原始视频中的时间位置
    // 简单处理：直接使用相同的时间，让解码器处理帧率差异
    int64_t clipLocalTime = timestamp - timelineStart + sourceStart;
    
    LOGCATE("%s: Processing frame %lld, timestamp=%lld, timelineStart=%lld, sourceStart=%lld, clipLocalTime=%lld, originalFPS=%d, targetFPS=%d", 
            TAG, (long long)frameIndex, (long long)timestamp, (long long)timelineStart, (long long)sourceStart, (long long)clipLocalTime, originalFPS, targetFPS);
    
    // 获取当前 clip 的索引
    int currentClipIndex = timeline->GetClipIndex(clip);
    
    // 剪辑切换或时间跳跃超过 1 秒时重新 seek
    bool clipChanged = (m_lastProcessedClip != clip) || (currentClipIndex != m_lastProcessedClipIndex);
    bool timeJumped = (clipLocalTime > m_lastProcessedTime + 1000000LL) || (clipLocalTime < m_lastProcessedTime - 100000LL);
    bool needSeek = clipChanged || timeJumped;
    
    LOGCATE("%s: Clip check - current=%d, last=%d, clipChanged=%d, timeJumped=%d, needSeek=%d", 
            TAG, currentClipIndex, m_lastProcessedClipIndex, clipChanged ? 1 : 0, timeJumped ? 1 : 0, needSeek ? 1 : 0);
    
    if (needSeek) {
        if (clip->SeekToTime(clipLocalTime) != 0) {
            LOGCATE("%s: SeekToTime failed for timestamp %lld", TAG, (long long)clipLocalTime);
            return -1;
        }
        m_lastProcessedClip = clip;
        m_lastProcessedClipIndex = currentClipIndex;
        m_lastProcessedTime = clipLocalTime;
        LOGCATE("%s: Performed seek to %lld (clip index=%d)", TAG, (long long)clipLocalTime, currentClipIndex);
    } else {
        // 不需要 seek，直接解码下一帧
        LOGCATE("%s: Skipping seek, using current position (clip=%d, time=%lld)", 
                TAG, currentClipIndex, (long long)clipLocalTime);
    }
    
    AVFrame* frame = nullptr;
    // 开始解码计时
    struct timespec decodeStart, decodeEnd;
    clock_gettime(CLOCK_MONOTONIC, &decodeStart);
    int decodeResult = clip->DecodeFrame(&frame);
    clock_gettime(CLOCK_MONOTONIC, &decodeEnd);
    double decodeTime = (decodeEnd.tv_sec - decodeStart.tv_sec) * 1000.0 + (decodeEnd.tv_nsec - decodeStart.tv_nsec) / 1000000.0;
    
    // 处理 EOF 情况：如果解码到文件末尾
    if (decodeResult == AVERROR_EOF) {
        LOGCATE("%s: Reached EOF for clip %d at time %lld", 
                TAG, currentClipIndex, (long long)clipLocalTime);
        
        // 计算该片段的实际结束时间
        int64_t clipEndTime = clip->GetTimelineStartTime() + clip->GetClipInfo().duration;
        
        // 直接跳到片段结束时间之后的下一个时间点
        int64_t nextTimestamp = clipEndTime + 1;
        TimelineClip* nextClip = timeline->GetClipAtTime(nextTimestamp);
        
        if (!nextClip) {
            // 没有更多片段，使用最后一帧填充剩余时间
            LOGCATE("%s: No more clips, reusing last frame for remaining frames", TAG);
            // 使用成员变量缓存的最后一帧
            if (m_lastFrameCache && m_lastFrameCache->data[0] != nullptr) {
                // 复制最后一帧
                av_frame_unref(m_eofFrameCache);
                av_frame_ref(m_eofFrameCache, m_lastFrameCache);
                frame = m_eofFrameCache;
                LOGCATE("%s: Reusing cached last frame", TAG);
            } else {
                LOGCATE("%s: No cached frame to reuse, failing", TAG);
                return -1;
            }
        } else {
            // 切换到下一个片段
            int64_t nextClipLocalTime = nextTimestamp - nextClip->GetTimelineStartTime() + nextClip->GetClipInfo().sourceStartTime;
            if (nextClip->SeekToTime(nextClipLocalTime) != 0) {
                LOGCATE("%s: SeekToTime failed for next clip", TAG);
                return -1;
            }
            
            m_lastProcessedClip = nextClip;
            m_lastProcessedClipIndex = timeline->GetClipIndex(nextClip);
            m_lastProcessedTime = nextClipLocalTime;
            
            // 重新解码
            decodeResult = nextClip->DecodeFrame(&frame);
            if (decodeResult != 0 || !frame) {
                LOGCATE("%s: DecodeFrame failed for next clip, result=%d", TAG, decodeResult);
                return -1;
            }
            
            LOGCATE("%s: Successfully switched to clip %d at timestamp %lld", 
                    TAG, m_lastProcessedClipIndex, (long long)nextTimestamp);
        }
    } else if (decodeResult != 0 || !frame) {
        LOGCATE("%s: DecodeFrame failed, result=%d, frame=%p", TAG, decodeResult, frame);
        return -1;
    }
    
    // 缓存最后一帧用于 EOF 时重复
    if (frame && frame->data[0] != nullptr) {
        av_frame_unref(m_lastFrameCache);
        av_frame_ref(m_lastFrameCache, frame);
    }
    
    m_lastProcessedTime = clipLocalTime;

    LOGCATE("%s: Decoded frame %dx%d, format=%d", TAG, frame->width, frame->height, frame->format);
    
    uint8_t* nv12Data = nullptr;
    int nv12Size = 0;
    
    // 开始渲染计时
    struct timespec renderStart, renderEnd;
    clock_gettime(CLOCK_MONOTONIC, &renderStart);
    // 使用 OpenGL 或 CPU 转换帧格式
    if (ConvertFrameToNV12(frame, &nv12Data, &nv12Size) != 0) {
        LOGCATE("%s: ConvertFrameToNV12 failed", TAG);
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &renderEnd);
    double renderTime = (renderEnd.tv_sec - renderStart.tv_sec) * 1000.0 + (renderEnd.tv_nsec - renderStart.tv_nsec) / 1000000.0;
    
    LOGCATE("%s: Frame converted, OpenGL=%s, size=%d", TAG, m_useOpenGL ? "yes" : "no", nv12Size);
    
    LOGCATE("%s: Converted to NV12, size=%d", TAG, nv12Size);
    
    // 应用CPU水印
    if (m_watermarkEnabled && m_cpuWatermark && m_cpuWatermark->IsValid()) {
        LOGCATE("%s: Applying CPU watermark", TAG);
        m_cpuWatermark->ApplyToNV12(nv12Data, m_config.width, m_config.height);
    }
    
    // 计算精确的presentationTimeUs，确保时间戳连续
    int64_t presentationTimeUs = (frameIndex * 1000000LL) / m_config.frameRate;
    
    // 确保时间戳单调递增
    if (m_lastPresentationPts >= 0 && presentationTimeUs <= m_lastPresentationPts) {
        presentationTimeUs = m_lastPresentationPts + 1;
    }
    m_lastPresentationPts = presentationTimeUs;
    
    LOGCATE("%s: Frame %lld, frameRate=%d, presentationTimeUs=%lld", 
            TAG, (long long)frameIndex, m_config.frameRate, (long long)presentationTimeUs);
    
    // 开始编码计时（仅统计输入提交时间）
    struct timespec encodeStart, encodeEnd;
    clock_gettime(CLOCK_MONOTONIC, &encodeStart);
    int result = FeedEncoderInput(nv12Data, nv12Size, presentationTimeUs);
    clock_gettime(CLOCK_MONOTONIC, &encodeEnd);
    double encodeTime = (encodeEnd.tv_sec - encodeStart.tv_sec) * 1000.0 + (encodeEnd.tv_nsec - encodeStart.tv_nsec) / 1000000.0;
    
    LOGCATE("%s: Fed encoder input, result=%d", TAG, result);
    
    // 异步处理输出，不阻塞当前帧处理
    // 每帧都尝试处理输出，但使用0超时避免阻塞
    DrainEncoder(false);
    
    if (nv12Data) {
        free(nv12Data);
    }
    
    LOGCATE("%s: Frame %lld processed successfully", TAG, (long long)frameIndex);
    
    // 保存性能数据
    FramePerformanceData perfData;
    perfData.frameIndex = frameIndex;
    perfData.decodeTime = decodeTime;
    perfData.renderTime = renderTime;
    perfData.encodeTime = encodeTime;
    m_framePerformanceData.push_back(perfData);
    
    return result;
}

int TimelineEncoder::ConvertFrameToNV12(AVFrame* srcFrame, uint8_t** dstData, int* dstSize) {
    // 如果 OpenGL 可用，使用 OpenGL 渲染
    if (m_useOpenGL && m_glRender) {
        return RenderFrameWithOpenGL(srcFrame, dstData, dstSize);
    }
    
    // CPU fallback
    if (!srcFrame || !dstData || !dstSize) {
        return -1;
    }
    
    int width = m_config.width;
    int height = m_config.height;
    int nv12Size = width * height * 3 / 2;
    
    *dstData = (uint8_t*)malloc(nv12Size);
    if (!*dstData) {
        LOGCATE("%s: malloc failed", TAG);
        return -1;
    }
    *dstSize = nv12Size;
    
    if (!m_swsContext) {
        m_swsContext = sws_getContext(srcFrame->width, srcFrame->height, 
                                      (AVPixelFormat)srcFrame->format,
                                      width, height, AV_PIX_FMT_NV12,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsContext) {
            LOGCATE("%s: sws_getContext failed", TAG);
            return -1;
        }
    }
    
    uint8_t* dstPlanes[2];
    int dstLinesize[2];
    
    dstPlanes[0] = *dstData;
    dstPlanes[1] = *dstData + width * height;
    dstLinesize[0] = width;
    dstLinesize[1] = width;
    
    sws_scale(m_swsContext, srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
              dstPlanes, dstLinesize);
    
    return 0;
}

int TimelineEncoder::FeedEncoderInput(uint8_t* data, int size, int64_t presentationTimeUs) {
    if (!m_videoCodec || !data) {
        return -1;
    }
    
    ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(m_videoCodec, TIMEOUT_US);
    if (inputIndex < 0) {
        if (inputIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            return 0;
        }
        LOGCATE("%s: dequeueInputBuffer failed, index=%zd", TAG, inputIndex);
        return -1;
    }
    
    size_t bufferSize = 0;
    uint8_t* inputBuffer = AMediaCodec_getInputBuffer(m_videoCodec, inputIndex, &bufferSize);
    if (!inputBuffer) {
        LOGCATE("%s: getInputBuffer failed", TAG);
        return -1;
    }
    
    size_t copySize = (size < (int)bufferSize) ? size : bufferSize;
    memcpy(inputBuffer, data, copySize);
    
    media_status_t status = AMediaCodec_queueInputBuffer(m_videoCodec, inputIndex, 0, copySize,
                                                          presentationTimeUs, 0);
    if (status != AMEDIA_OK) {
        LOGCATE("%s: queueInputBuffer failed, status=%d", TAG, status);
        return -1;
    }
    
    return 0;
}

int TimelineEncoder::DrainEncoder(bool endOfStream) {
    if (!m_videoCodec) {
        return -1;
    }
    
    if (endOfStream) {
        ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(m_videoCodec, TIMEOUT_US);
        if (inputIndex >= 0) {
            AMediaCodec_queueInputBuffer(m_videoCodec, inputIndex, 0, 0, 0, 
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        }
    }
    
    AMediaCodecBufferInfo info;
    ssize_t outputIndex = 0;
    int outputFrameCount = 0;
    
    while (true) {
        // 非结束状态时使用0超时，避免阻塞渲染线程
        int64_t timeout = endOfStream ? TIMEOUT_US : 0;
        outputIndex = AMediaCodec_dequeueOutputBuffer(m_videoCodec, &info, timeout);
        
        if (outputIndex < 0) {
            if (outputIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                // 非结束状态下，没有数据立即退出，不阻塞
                break;
            } else if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (m_videoTrackIndex < 0) {
                    AMediaFormat* format = AMediaCodec_getOutputFormat(m_videoCodec);
                    LOGCATE("%s: Output format changed, starting muxer", TAG);
                    m_videoTrackIndex = AMediaMuxer_addTrack(m_mediaMuxer, format);
                    media_status_t startStatus = AMediaMuxer_start(m_mediaMuxer);
                    LOGCATE("%s: Muxer started, trackIndex=%d, status=%d", TAG, m_videoTrackIndex, startStatus);
                    AMediaFormat_delete(format);
                }
            }
            continue;
        }
        
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
            AMediaCodec_releaseOutputBuffer(m_videoCodec, outputIndex, false);
            break;
        }
        
        if (info.size > 0) {
            size_t bufferSize = 0;
            uint8_t* outputBuffer = AMediaCodec_getOutputBuffer(m_videoCodec, outputIndex, &bufferSize);
            
            LOGCATE("%s: Output frame pts=%lld, size=%d", TAG, (long long)info.presentationTimeUs, info.size);
            outputFrameCount++;
            
            if (outputBuffer && m_videoTrackIndex >= 0 && m_mediaMuxer) {
                media_status_t status = AMediaMuxer_writeSampleData(m_mediaMuxer, m_videoTrackIndex, 
                                            outputBuffer + info.offset, &info);
                if (status != AMEDIA_OK) {
                    LOGCATE("%s: writeSampleData failed, status=%d", TAG, status);
                    if (status == AMEDIA_ERROR_INVALID_OPERATION) {
                        AMediaCodec_releaseOutputBuffer(m_videoCodec, outputIndex, false);
                        return -1;
                    }
                }
            } else {
                LOGCATE("%s: Cannot write - outputBuffer=%p, trackIndex=%d, muxer=%p", 
                        TAG, outputBuffer, m_videoTrackIndex, m_mediaMuxer);
            }
        }
        
        AMediaCodec_releaseOutputBuffer(m_videoCodec, outputIndex, false);
    }
    
    if (outputFrameCount > 0) {
        LOGCATE("%s: DrainEncoder output %d frames", TAG, outputFrameCount);
    }
    
    return 0;
}

int TimelineEncoder::EncodeFrame(NativeImage* frame, int64_t presentationTimeUs) {
    if (!frame || !frame->ppPlane[0]) {
        return -1;
    }
    
    uint8_t* nv12Data = nullptr;
    int nv12Size = 0;
    
    return FeedEncoderInput(frame->ppPlane[0], frame->width * frame->height * 3 / 2, presentationTimeUs);
}

void TimelineEncoder::SetWatermark(const char* imagePath, int x, int y, float opacity, float scale) {
    LOGCATE("%s: SetWatermark called (path=%s, useOpenGL=%d, glRender=%p)", TAG, 
            imagePath ? imagePath : "(null)", m_useOpenGL, m_glRender);
    
    // 如果 OpenGL 可用，保存水印信息为 pending，在编码线程初始化后设置
    if (m_useOpenGL && imagePath) {
        // 计算实际位置 - 保存边距值（0.05表示5%）
        float margin = 0.05f;  // 边距 5%
        
        m_pendingWatermarkPath = imagePath;
        m_pendingWatermarkOpacity = opacity;
        m_pendingWatermarkScale = scale;
        m_pendingWatermarkOffsetX = margin;
        m_pendingWatermarkOffsetY = margin;
        m_pendingWatermarkSet = true;
        LOGCATE("%s: Saved pending OpenGL watermark for later (path=%s)", TAG, imagePath);
    } else {
        // CPU fallback
        if (!m_cpuWatermark) {
            m_cpuWatermark = new CPUWatermarkFilter();
        }
        
        if (imagePath) {
            int ret = m_cpuWatermark->LoadWatermark(imagePath);
            if (ret == 0) {
                // 计算实际位置 (右下角，考虑缩放后的水印大小)
                int scaledW = (int)(m_cpuWatermark->GetWidth() * scale);
                int scaledH = (int)(m_cpuWatermark->GetHeight() * scale);
                int actualX = m_config.width - scaledW - (int)(m_config.width * 0.05f);  // 右边距 5%
                int actualY = m_config.height - scaledH - (int)(m_config.height * 0.05f); // 下边距 5%
                
                m_cpuWatermark->SetPosition(actualX, actualY);
                m_cpuWatermark->SetOpacity(opacity);
                m_cpuWatermark->SetScale(scale);
                LOGCATE("%s: Watermark loaded and configured at (%d,%d), size=%dx%d", 
                        TAG, actualX, actualY, scaledW, scaledH);
            } else {
                LOGCATE("%s: Failed to load watermark: %s", TAG, imagePath);
            }
        }
    }
}

int TimelineEncoder::RenderFrameWithOpenGL(AVFrame* srcFrame, uint8_t** dstData, int* dstSize) {
    if (!m_glRender || !srcFrame || !dstData || !dstSize) {
        return -1;
    }
    
    uint8_t* nv12Data = nullptr;
    int nv12Size = 0;
    
    // 使用 OpenGL 渲染器进行帧转换
    int ret = m_glRender->RenderFrame(srcFrame, &nv12Data, &nv12Size);
    if (ret != 0) {
        LOGCATE("%s: OpenGL RenderFrame failed, ret=%d", TAG, ret);
        return -1;
    }
    
    *dstData = nv12Data;
    *dstSize = nv12Size;
    
    LOGCATE("%s: OpenGL render successful, size=%d", TAG, nv12Size);
    return 0;
}

int TimelineEncoder::Finalize() {
    DrainEncoder(true);
    
    if (m_mediaMuxer) {
        AMediaMuxer_stop(m_mediaMuxer);
    }
    
    // 记录导出结束时间
    clock_gettime(CLOCK_MONOTONIC, &m_exportEndTime);
    
    // 保存性能数据
    SavePerformanceData();
    
    return 0;
}

double TimelineEncoder::CalculatePercentile(const std::vector<double>& data, double percentile) {
    if (data.empty()) {
        return 0.0;
    }
    
    std::vector<double> sortedData = data;
    std::sort(sortedData.begin(), sortedData.end());
    
    double index = (percentile / 100.0) * (sortedData.size() - 1);
    int lowerIndex = (int)index;
    int upperIndex = lowerIndex + 1;
    
    if (upperIndex >= (int)sortedData.size()) {
        return sortedData[lowerIndex];
    }
    
    double fraction = index - lowerIndex;
    return sortedData[lowerIndex] + fraction * (sortedData[upperIndex] - sortedData[lowerIndex]);
}

void TimelineEncoder::SavePerformanceData() {
    if (m_framePerformanceData.empty()) {
        LOGCATE("%s: No performance data to save", TAG);
        return;
    }
    
    // 计算导出总耗时（秒）
    double totalExportTime = (m_exportEndTime.tv_sec - m_exportStartTime.tv_sec) + 
                           (m_exportEndTime.tv_nsec - m_exportStartTime.tv_nsec) / 1000000000.0;
    
    // 收集各项耗时数据
    std::vector<double> decodeTimes;
    std::vector<double> renderTimes;
    std::vector<double> encodeTimes;
    
    for (const auto& perf : m_framePerformanceData) {
        decodeTimes.push_back(perf.decodeTime);
        renderTimes.push_back(perf.renderTime);
        encodeTimes.push_back(perf.encodeTime);
    }
    
    // 计算平均值
    double avgDecode = 0.0, avgRender = 0.0, avgEncode = 0.0;
    for (const auto& perf : m_framePerformanceData) {
        avgDecode += perf.decodeTime;
        avgRender += perf.renderTime;
        avgEncode += perf.encodeTime;
    }
    avgDecode /= m_framePerformanceData.size();
    avgRender /= m_framePerformanceData.size();
    avgEncode /= m_framePerformanceData.size();
    
    // 计算百分位数
    double p5Decode = CalculatePercentile(decodeTimes, 5.0);
    double p50Decode = CalculatePercentile(decodeTimes, 50.0);
    double p95Decode = CalculatePercentile(decodeTimes, 95.0);
    
    double p5Render = CalculatePercentile(renderTimes, 5.0);
    double p50Render = CalculatePercentile(renderTimes, 50.0);
    double p95Render = CalculatePercentile(renderTimes, 95.0);
    
    double p5Encode = CalculatePercentile(encodeTimes, 5.0);
    double p50Encode = CalculatePercentile(encodeTimes, 50.0);
    double p95Encode = CalculatePercentile(encodeTimes, 95.0);
    
    // 计算平均每秒导出帧数
    double avgFps = m_framePerformanceData.size() / totalExportTime;
    
    // 生成JSON文件路径
    std::string outputPath = m_config.outputPath;
    size_t dotPos = outputPath.find_last_of('.');
    std::string jsonPath = outputPath.substr(0, dotPos) + ".json";
    
    // 构建JSON内容
    std::stringstream json;
    json << "{\n";
    json << "  \"summary\": {\n";
    json << "    \"totalExportTime\": " << totalExportTime << ",\n";
    json << "    \"totalFrames\": " << m_framePerformanceData.size() << ",\n";
    json << "    \"avgFps\": " << avgFps << "\n";
    json << "  },\n";
    json << "  \"decode\": {\n";
    json << "    \"avg\": " << avgDecode << ",\n";
    json << "    \"p5\": " << p5Decode << ",\n";
    json << "    \"p50\": " << p50Decode << ",\n";
    json << "    \"p95\": " << p95Decode << "\n";
    json << "  },\n";
    json << "  \"render\": {\n";
    json << "    \"avg\": " << avgRender << ",\n";
    json << "    \"p5\": " << p5Render << ",\n";
    json << "    \"p50\": " << p50Render << ",\n";
    json << "    \"p95\": " << p95Render << "\n";
    json << "  },\n";
    json << "  \"encode\": {\n";
    json << "    \"avg\": " << avgEncode << ",\n";
    json << "    \"p5\": " << p5Encode << ",\n";
    json << "    \"p50\": " << p50Encode << ",\n";
    json << "    \"p95\": " << p95Encode << "\n";
    json << "  },\n";
    json << "  \"frames\": [\n";
    
    for (size_t i = 0; i < m_framePerformanceData.size(); i++) {
        const auto& perf = m_framePerformanceData[i];
        json << "    {\n";
        json << "      \"frameIndex\": " << perf.frameIndex << ",\n";
        json << "      \"decodeTime\": " << perf.decodeTime << ",\n";
        json << "      \"renderTime\": " << perf.renderTime << ",\n";
        json << "      \"encodeTime\": " << perf.encodeTime << "\n";
        json << "    }";
        if (i < m_framePerformanceData.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    
    // 写入文件
    std::ofstream file(jsonPath);
    if (file.is_open()) {
        file << json.str();
        file.close();
        LOGCATE("%s: Performance data saved to %s", TAG, jsonPath.c_str());
    } else {
        LOGCATE("%s: Failed to open file for writing: %s", TAG, jsonPath.c_str());
    }
}
