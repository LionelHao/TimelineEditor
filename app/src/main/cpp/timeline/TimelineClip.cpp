#include "TimelineClip.h"
#include "LogUtil.h"

#define TAG "TimelineClip"

TimelineClip::TimelineClip()
    : m_state(ClipState::CLIP_STATE_IDLE)
    , m_pFormatCtx(nullptr)
    , m_pCodecCtx(nullptr)
    , m_pVideoStream(nullptr)
    , m_videoStreamIndex(-1)
    , m_pFrame(nullptr)
    , m_pPacket(nullptr)
    , m_pSwsContext(nullptr)
    , m_currentPts(0)
    , m_isOpened(false)
{
}

TimelineClip::~TimelineClip() {
    UnInit();
}

int TimelineClip::Init(const char* sourcePath, int64_t timelineStartTime) {
    if (sourcePath == nullptr) {
        LOGCATE("%s: sourcePath is null", TAG);
        return -1;
    }

    m_clipInfo.sourcePath = sourcePath;
    m_clipInfo.timelineStartTime = timelineStartTime;
    m_clipInfo.sourceStartTime = 0;
    
    return 0;
}

void TimelineClip::UnInit() {
    CloseSource();
    m_state = ClipState::CLIP_STATE_IDLE;
}

int TimelineClip::OpenSource() {
    if (m_isOpened) {
        return 0;
    }

    int ret = InitDecoder();
    if (ret != 0) {
        LOGCATE("%s: InitDecoder failed, ret=%d", TAG, ret);
        return ret;
    }

    m_isOpened = true;
    m_state = ClipState::CLIP_STATE_READY;
    UpdateClipInfo();

    return 0;
}

void TimelineClip::CloseSource() {
    if (m_pSwsContext) {
        sws_freeContext(m_pSwsContext);
        m_pSwsContext = nullptr;
    }

    if (m_pPacket) {
        av_packet_free(&m_pPacket);
        m_pPacket = nullptr;
    }

    if (m_pFrame) {
        av_frame_free(&m_pFrame);
        m_pFrame = nullptr;
    }

    if (m_pCodecCtx) {
        avcodec_free_context(&m_pCodecCtx);
        m_pCodecCtx = nullptr;
    }

    if (m_pFormatCtx) {
        avformat_close_input(&m_pFormatCtx);
        m_pFormatCtx = nullptr;
    }

    m_isOpened = false;
    m_state = ClipState::CLIP_STATE_IDLE;
}

int TimelineClip::InitDecoder() {
    int ret = avformat_open_input(&m_pFormatCtx, m_clipInfo.sourcePath.c_str(), nullptr, nullptr);
    if (ret != 0) {
        LOGCATE("%s: avformat_open_input failed, ret=%d", TAG, ret);
        return ret;
    }

    ret = avformat_find_stream_info(m_pFormatCtx, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avformat_find_stream_info failed, ret=%d", TAG, ret);
        return ret;
    }

    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_pFormatCtx->nb_streams; i++) {
        if (m_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            m_pVideoStream = m_pFormatCtx->streams[i];
            break;
        }
    }

    if (m_videoStreamIndex == -1) {
        LOGCATE("%s: No video stream found", TAG);
        return -1;
    }

    const AVCodec* codec = avcodec_find_decoder(m_pVideoStream->codecpar->codec_id);
    if (!codec) {
        LOGCATE("%s: avcodec_find_decoder failed", TAG);
        return -1;
    }

    m_pCodecCtx = avcodec_alloc_context3(codec);
    if (!m_pCodecCtx) {
        LOGCATE("%s: avcodec_alloc_context3 failed", TAG);
        return -1;
    }

    ret = avcodec_parameters_to_context(m_pCodecCtx, m_pVideoStream->codecpar);
    if (ret < 0) {
        LOGCATE("%s: avcodec_parameters_to_context failed, ret=%d", TAG, ret);
        return ret;
    }

    ret = avcodec_open2(m_pCodecCtx, codec, nullptr);
    if (ret < 0) {
        LOGCATE("%s: avcodec_open2 failed, ret=%d", TAG, ret);
        return ret;
    }

    m_pFrame = av_frame_alloc();
    m_pPacket = av_packet_alloc();

    if (!m_pFrame || !m_pPacket) {
        LOGCATE("%s: av_frame_alloc or av_packet_alloc failed", TAG);
        return -1;
    }

    return 0;
}

void TimelineClip::UpdateClipInfo() {
    if (m_pCodecCtx) {
        m_clipInfo.width = m_pCodecCtx->width;
        m_clipInfo.height = m_pCodecCtx->height;
    }

    if (m_pVideoStream) {
        AVRational frameRate = av_guess_frame_rate(m_pFormatCtx, m_pVideoStream, nullptr);
        m_clipInfo.frameRate = frameRate.num / frameRate.den;
        
        LOGCATE("%s: stream duration=%lld, format duration=%lld, time_base=%d/%d", 
                TAG, (long long)m_pVideoStream->duration, (long long)m_pFormatCtx->duration,
                m_pVideoStream->time_base.num, m_pVideoStream->time_base.den);
        
        if (m_pVideoStream->duration != AV_NOPTS_VALUE) {
            m_clipInfo.duration = av_rescale_q(m_pVideoStream->duration, 
                                               m_pVideoStream->time_base, 
                                               AV_TIME_BASE_Q);
            LOGCATE("%s: Using stream duration, rescaled to %lld", TAG, (long long)m_clipInfo.duration);
        } else if (m_pFormatCtx->duration != AV_NOPTS_VALUE) {
            m_clipInfo.duration = m_pFormatCtx->duration;
            LOGCATE("%s: Using format duration: %lld", TAG, (long long)m_clipInfo.duration);
        } else {
            m_clipInfo.duration = 0;
            LOGCATE("%s: No duration available!", TAG);
        }

        AVDictionaryEntry* rotationEntry = av_dict_get(m_pVideoStream->metadata, "rotate", nullptr, 0);
        if (rotationEntry) {
            m_clipInfo.rotation = atoi(rotationEntry->value);
        } else {
            m_clipInfo.rotation = 0;
        }
    }

    m_clipInfo.sourceEndTime = m_clipInfo.duration;
    LOGCATE("%s: width=%d, height=%d, duration=%lld us (%.2f s), fps=%d", 
            TAG, m_clipInfo.width, m_clipInfo.height, 
            (long long)m_clipInfo.duration, m_clipInfo.duration / 1000000.0f, m_clipInfo.frameRate);
}

int TimelineClip::SeekToTime(int64_t timestamp) {
    if (!m_isOpened) {
        LOGCATE("%s: Clip not opened", TAG);
        return -1;
    }

    int64_t seekTarget = av_rescale_q(timestamp, AV_TIME_BASE_Q, m_pVideoStream->time_base);
    
    int ret = av_seek_frame(m_pFormatCtx, m_videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOGCATE("%s: av_seek_frame failed, ret=%d", TAG, ret);
        return ret;
    }

    avcodec_flush_buffers(m_pCodecCtx);
    m_currentPts = timestamp;

    return 0;
}

int TimelineClip::DecodeFrame(AVFrame** ppFrame) {
    if (!m_isOpened || !ppFrame) {
        return -1;
    }

    int ret;
    bool gotFrame = false;

    while (!gotFrame) {
        ret = av_read_frame(m_pFormatCtx, m_pPacket);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                return AVERROR_EOF;
            }
            LOGCATE("%s: av_read_frame failed, ret=%d", TAG, ret);
            return ret;
        }

        if (m_pPacket->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_pPacket);
            continue;
        }

        ret = avcodec_send_packet(m_pCodecCtx, m_pPacket);
        if (ret < 0) {
            LOGCATE("%s: avcodec_send_packet failed, ret=%d", TAG, ret);
            av_packet_unref(m_pPacket);
            return ret;
        }

        ret = avcodec_receive_frame(m_pCodecCtx, m_pFrame);
        if (ret == 0) {
            gotFrame = true;
            *ppFrame = m_pFrame;
            
            if (m_pFrame->pts != AV_NOPTS_VALUE) {
                m_currentPts = av_rescale_q(m_pFrame->pts, 
                                            m_pVideoStream->time_base, 
                                            AV_TIME_BASE_Q);
            }
        } else if (ret == AVERROR(EAGAIN)) {
            av_packet_unref(m_pPacket);
            continue;
        } else {
            LOGCATE("%s: avcodec_receive_frame failed, ret=%d", TAG, ret);
            av_packet_unref(m_pPacket);
            return ret;
        }

        av_packet_unref(m_pPacket);
    }

    return 0;
}

void TimelineClip::SetSourceRange(int64_t startTime, int64_t endTime) {
    m_clipInfo.sourceStartTime = startTime;
    m_clipInfo.sourceEndTime = endTime;
    m_clipInfo.duration = endTime - startTime;
}

void TimelineClip::SetTimelinePosition(int64_t position) {
    m_clipInfo.timelineStartTime = position;
}

bool TimelineClip::IsFrameAvailable() const {
    return m_isOpened && m_state == ClipState::CLIP_STATE_READY;
}
