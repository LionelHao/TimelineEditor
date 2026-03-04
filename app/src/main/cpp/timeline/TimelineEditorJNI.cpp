#include <jni.h>
#include <string>
#include "TimelineEditor.h"
#include "LogUtil.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>

#define TAG "TimelineEditorJNI"

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1Init(JNIEnv* env, jobject thiz) {
    TimelineEditor* editor = TimelineEditor::GetInstance();
    if (editor && editor->Init(env, thiz) == 0) {
        return reinterpret_cast<jlong>(editor);
    }
    return 0;
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1UnInit(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->UnInit();
    }
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1CreateTimeline(JNIEnv* env, jobject thiz, 
                                                                           jlong handle, jint width, 
                                                                           jint height, jint frameRate) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->CreateTimeline(width, height, frameRate);
    }
    return -1;
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1DestroyTimeline(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->DestroyTimeline();
    }
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1AddVideoClip(JNIEnv* env, jobject thiz, 
                                                                         jlong handle, jstring filePath) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor && filePath) {
        const char* path = env->GetStringUTFChars(filePath, nullptr);
        int result = editor->AddVideoClip(path);
        env->ReleaseStringUTFChars(filePath, path);
        return result;
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1AddVideoClipAtPosition(JNIEnv* env, jobject thiz, 
                                                                                   jlong handle, jstring filePath, 
                                                                                   jlong positionMs) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor && filePath) {
        const char* path = env->GetStringUTFChars(filePath, nullptr);
        int result = editor->AddVideoClipAtPosition(path, positionMs);
        env->ReleaseStringUTFChars(filePath, path);
        return result;
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1RemoveVideoClip(JNIEnv* env, jobject thiz, 
                                                                            jlong handle, jint index) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->RemoveVideoClip(index);
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1MoveVideoClip(JNIEnv* env, jobject thiz, 
                                                                          jlong handle, jint fromIndex, 
                                                                          jint toIndex) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->MoveVideoClip(fromIndex, toIndex);
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1GetClipCount(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->GetClipCount();
    }
    return 0;
}

JNIEXPORT jlong JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1GetTimelineDuration(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->GetTimelineDuration();
    }
    return 0;
}

JNIEXPORT jlong JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1GetCurrentPosition(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->GetCurrentPosition();
    }
    return 0;
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1SetCurrentPosition(JNIEnv* env, jobject thiz, 
                                                                               jlong handle, jlong positionMs) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->SetCurrentPosition(positionMs);
    }
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1SetWatermark(JNIEnv* env, jobject thiz, 
                                                                         jlong handle, jstring imagePath, 
                                                                         jint position, jfloat opacity, 
                                                                         jfloat scale) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        const char* path = imagePath ? env->GetStringUTFChars(imagePath, nullptr) : nullptr;
        int result = editor->SetWatermark(path, position, opacity, scale);
        if (path) {
            env->ReleaseStringUTFChars(imagePath, path);
        }
        return result;
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1SetHistogramEqualization(JNIEnv* env, jobject thiz, 
                                                                                      jlong handle, jboolean enabled, 
                                                                                      jfloat intensity) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->SetHistogramEqualization(enabled, intensity);
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1SetOutputPath(JNIEnv* env, jobject thiz, 
                                                                          jlong handle, jstring outputPath) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor && outputPath) {
        const char* path = env->GetStringUTFChars(outputPath, nullptr);
        int result = editor->SetOutputPath(path);
        env->ReleaseStringUTFChars(outputPath, path);
        return result;
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1SetExportParams(JNIEnv* env, jobject thiz, 
                                                                            jlong handle, jint width, 
                                                                            jint height, jint bitrate) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->SetExportParams(width, height, bitrate);
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1StartExport(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->StartExport();
    }
    return -1;
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1StopExport(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->StopExport();
    }
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1PauseExport(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->PauseExport();
    }
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1ResumeExport(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->ResumeExport();
    }
}

JNIEXPORT jfloat JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1GetExportProgress(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->GetExportProgress();
    }
    return 0.0f;
}

JNIEXPORT jboolean JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1IsExporting(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->IsExporting();
    }
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1SetPreviewSurface(JNIEnv* env, jobject thiz, 
                                                                              jlong handle, jobject surface) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        ANativeWindow* window = nullptr;
        if (surface != nullptr) {
            window = ANativeWindow_fromSurface(env, surface);
        }
        return editor->SetPreviewSurface(window);
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1StartPreview(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->StartPreview();
    }
    return -1;
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1StopPreview(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->StopPreview();
    }
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1PausePreview(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->PausePreview();
    }
}

JNIEXPORT void JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1ResumePreview(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        editor->ResumePreview();
    }
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1PreviewFrame(JNIEnv* env, jobject thiz, 
                                                                         jlong handle, jlong positionMs) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->PreviewFrame(positionMs);
    }
    return -1;
}

JNIEXPORT jboolean JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1IsPreviewing(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->IsPreviewing();
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1IsPreviewPaused(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->IsPreviewPaused();
    }
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1OnSurfaceCreated(JNIEnv* env, jobject thiz, jlong handle, jint renderType) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->OnSurfaceCreated(renderType);
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1OnSurfaceChanged(JNIEnv* env, jobject thiz, jlong handle, jint renderType, jint width, jint height) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->OnSurfaceChanged(renderType, width, height);
    }
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_lionel_timelineeditor_TimelineEditor_native_1OnDrawFrame(JNIEnv* env, jobject thiz, jlong handle, jint renderType) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->OnDrawFrame(renderType);
    }
    return -1;
}

jboolean Java_com_lionel_timelineeditor_TimelineEditor_native_1CheckAndClearNeedRender(JNIEnv* env, jobject thiz, jlong handle) {
    TimelineEditor* editor = reinterpret_cast<TimelineEditor*>(handle);
    if (editor) {
        return editor->CheckAndClearNeedRender() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

}
