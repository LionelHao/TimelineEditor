/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#ifndef LEARNFFMPEG_NATIVERENDER_H
#define LEARNFFMPEG_NATIVERENDER_H

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <algorithm>

#include "LogUtil.h"

extern "C" {
#include <libavutil/frame.h>
}

class NativeRender {
public:
    NativeRender(ANativeWindow* window);
    ~NativeRender();

    void Init(int videoWidth, int videoHeight, int *dstSize);
    void RenderVideoFrame(AVFrame *frame);
    void UnInit();
    
    int GetDstWidth() const { return m_DstWidth; }
    int GetDstHeight() const { return m_DstHeight; }

private:
    ANativeWindow *m_NativeWindow = nullptr;
    ANativeWindow_Buffer m_NativeWindowBuffer;
    int m_DstWidth = 0;
    int m_DstHeight = 0;
};

#endif //LEARNFFMPEG_NATIVERENDER_H
