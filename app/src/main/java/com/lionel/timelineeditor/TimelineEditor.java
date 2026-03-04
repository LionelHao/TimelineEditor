package com.lionel.timelineeditor;

import android.view.Surface;

public class TimelineEditor {
    static {
        System.loadLibrary("timeline-editor");
    }

    public static final int WATERMARK_POSITION_TOP_LEFT = 0;
    public static final int WATERMARK_POSITION_TOP_RIGHT = 1;
    public static final int WATERMARK_POSITION_BOTTOM_LEFT = 2;
    public static final int WATERMARK_POSITION_BOTTOM_RIGHT = 3;
    public static final int WATERMARK_POSITION_CENTER = 4;
    public static final int WATERMARK_POSITION_CUSTOM = 5;

    public static final int MSG_EXPORT_PROGRESS = 100;
    public static final int MSG_EXPORT_COMPLETE = 101;
    public static final int MSG_EXPORT_ERROR = 102;
    public static final int MSG_PREVIEW_FRAME = 200;
    public static final int MSG_PREVIEW_POSITION = 201;

    public static final int RENDER_TYPE_OPENGL = 0;
    public static final int RENDER_TYPE_ANWINDOW = 1;

    private long mNativeHandle = 0;
    private EventCallback mEventCallback = null;

    public interface EventCallback {
        void onEditorEvent(int msgType, float msgValue);
    }

    public void init() {
        mNativeHandle = native_Init();
    }

    public void unInit() {
        native_UnInit(mNativeHandle);
        mNativeHandle = 0;
    }

    public int createTimeline(int width, int height, int frameRate) {
        return native_CreateTimeline(mNativeHandle, width, height, frameRate);
    }

    public void destroyTimeline() {
        native_DestroyTimeline(mNativeHandle);
    }

    public int addVideoClip(String filePath) {
        return native_AddVideoClip(mNativeHandle, filePath);
    }

    public int addVideoClipAtPosition(String filePath, long positionMs) {
        return native_AddVideoClipAtPosition(mNativeHandle, filePath, positionMs);
    }

    public int removeVideoClip(int index) {
        return native_RemoveVideoClip(mNativeHandle, index);
    }

    public int moveVideoClip(int fromIndex, int toIndex) {
        return native_MoveVideoClip(mNativeHandle, fromIndex, toIndex);
    }

    public int getClipCount() {
        return native_GetClipCount(mNativeHandle);
    }

    public long getTimelineDuration() {
        return native_GetTimelineDuration(mNativeHandle);
    }

    public long getCurrentPosition() {
        return native_GetCurrentPosition(mNativeHandle);
    }

    public void setCurrentPosition(long positionMs) {
        native_SetCurrentPosition(mNativeHandle, positionMs);
    }

    public void seekToPosition(int positionMs) {
        native_SetCurrentPosition(mNativeHandle, positionMs);
    }

    public int setWatermark(String imagePath, int position, float opacity, float scale) {
        return native_SetWatermark(mNativeHandle, imagePath, position, opacity, scale);
    }

    public int setHistogramEqualization(boolean enabled, float intensity) {
        return native_SetHistogramEqualization(mNativeHandle, enabled, intensity);
    }

    public int setOutputPath(String outputPath) {
        return native_SetOutputPath(mNativeHandle, outputPath);
    }

    public int setExportParams(int width, int height, int bitrate) {
        return native_SetExportParams(mNativeHandle, width, height, bitrate);
    }

    public int startExport() {
        return native_StartExport(mNativeHandle);
    }

    public void stopExport() {
        native_StopExport(mNativeHandle);
    }

    public void pauseExport() {
        native_PauseExport(mNativeHandle);
    }

    public void resumeExport() {
        native_ResumeExport(mNativeHandle);
    }

    public float getExportProgress() {
        return native_GetExportProgress(mNativeHandle);
    }

    public boolean isExporting() {
        return native_IsExporting(mNativeHandle);
    }

    public int setPreviewSurface(Surface surface) {
        return native_SetPreviewSurface(mNativeHandle, surface);
    }

    public int startPreview() {
        return native_StartPreview(mNativeHandle);
    }

    public void stopPreview() {
        native_StopPreview(mNativeHandle);
    }

    public void pausePreview() {
        native_PausePreview(mNativeHandle);
    }

    public void resumePreview() {
        native_ResumePreview(mNativeHandle);
    }

    public int previewFrame(long positionMs) {
        return native_PreviewFrame(mNativeHandle, positionMs);
    }

    public boolean isPreviewing() {
        return native_IsPreviewing(mNativeHandle);
    }

    public boolean isPreviewPaused() {
        return native_IsPreviewPaused(mNativeHandle);
    }

    public int onSurfaceCreated(int renderType) {
        return native_OnSurfaceCreated(mNativeHandle, renderType);
    }

    public int onSurfaceChanged(int renderType, int width, int height) {
        return native_OnSurfaceChanged(mNativeHandle, renderType, width, height);
    }

    public int onDrawFrame(int renderType) {
        return native_OnDrawFrame(mNativeHandle, renderType);
    }

    public boolean checkAndClearNeedRender() {
        return native_CheckAndClearNeedRender(mNativeHandle);
    }

    public void addEventCallback(EventCallback callback) {
        mEventCallback = callback;
    }

    public void onEditorEvent(int msgType, float msgValue) {
        if (mEventCallback != null) {
            mEventCallback.onEditorEvent(msgType, msgValue);
        }
    }

    private native long native_Init();
    private native void native_UnInit(long handle);
    private native int native_CreateTimeline(long handle, int width, int height, int frameRate);
    private native void native_DestroyTimeline(long handle);
    private native int native_AddVideoClip(long handle, String filePath);
    private native int native_AddVideoClipAtPosition(long handle, String filePath, long positionMs);
    private native int native_RemoveVideoClip(long handle, int index);
    private native int native_MoveVideoClip(long handle, int fromIndex, int toIndex);
    private native int native_GetClipCount(long handle);
    private native long native_GetTimelineDuration(long handle);
    private native long native_GetCurrentPosition(long handle);
    private native void native_SetCurrentPosition(long handle, long positionMs);
    private native int native_SetWatermark(long handle, String imagePath, int position, float opacity, float scale);
    private native int native_SetHistogramEqualization(long handle, boolean enabled, float intensity);
    private native int native_SetOutputPath(long handle, String outputPath);
    private native int native_SetExportParams(long handle, int width, int height, int bitrate);
    private native int native_StartExport(long handle);
    private native void native_StopExport(long handle);
    private native void native_PauseExport(long handle);
    private native void native_ResumeExport(long handle);
    private native float native_GetExportProgress(long handle);
    private native boolean native_IsExporting(long handle);

    private native int native_SetPreviewSurface(long handle, Surface surface);
    private native int native_StartPreview(long handle);
    private native void native_StopPreview(long handle);
    private native void native_PausePreview(long handle);
    private native void native_ResumePreview(long handle);
    private native int native_PreviewFrame(long handle, long positionMs);
    private native boolean native_IsPreviewing(long handle);
    private native boolean native_IsPreviewPaused(long handle);

    private native int native_OnSurfaceCreated(long handle, int renderType);
    private native int native_OnSurfaceChanged(long handle, int renderType, int width, int height);
    private native int native_OnDrawFrame(long handle, int renderType);
    private native boolean native_CheckAndClearNeedRender(long handle);
}
