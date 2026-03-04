package com.lionel.timelineeditor;

import android.Manifest;
import android.app.ProgressDialog;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ArrayAdapter;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class TimelineEditorActivity extends AppCompatActivity implements View.OnClickListener, GLSurfaceView.Renderer, TimelineEditor.EventCallback {
    private static final String TAG = "TimelineEditorActivity";
    private static final int PERMISSION_REQUEST_CODE = 1;

    private static final String[] REQUIRED_PERMISSIONS = {
            Manifest.permission.READ_EXTERNAL_STORAGE,
            Manifest.permission.WRITE_EXTERNAL_STORAGE
    };

    private TimelineEditor mTimelineEditor;
    private LinearLayout mClipListLayout;
    private TextView mDurationText;
    private TextView mProgressText;
    private SeekBar mTimelineSeekBar;
    private GLSurfaceView mGLSurfaceView;
    private Button mBtnPlay;
    private Button mBtnPause;
    private Button mBtnStop;
    private Button mBtnAddVideo;
    private Button mBtnAddWatermark;
    private Button mBtnExport;
    private Button mBtnRemoveClip;
    private EditText mEtWidth;
    private EditText mEtHeight;
    private EditText mEtBitrate;
    private Spinner mSpCodecMode;
    private Spinner mSpRenderFilter;
    private ProgressDialog mProgressDialog;
    private Button mBtnToggleParams;
    private LinearLayout mExportParamsContainer;

    private List<String> mSelectedVideos = new ArrayList<>();
    private String mWatermarkPath;
    private int mSelectedClipIndex = -1;
    private boolean mIsPlaying = false;
    private boolean mSurfaceCreated = false;
    private boolean mParamsExpanded = false;
    private boolean mIsTouch = false;

    private Handler mHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_timeline_editor);

        initViews();
        checkPermissions();
        initTimelineEditor();
    }

    private void initViews() {
        mClipListLayout = findViewById(R.id.clip_list_layout);
        mDurationText = findViewById(R.id.duration_text);
        mProgressText = findViewById(R.id.progress_text);
        mTimelineSeekBar = findViewById(R.id.timeline_seekbar);
        mGLSurfaceView = findViewById(R.id.gl_surface_view);
        mBtnPlay = findViewById(R.id.btn_play);
        mBtnPause = findViewById(R.id.btn_pause);
        mBtnStop = findViewById(R.id.btn_stop);
        mBtnAddVideo = findViewById(R.id.btn_add_video);
        mBtnAddWatermark = findViewById(R.id.btn_add_watermark);
        mBtnExport = findViewById(R.id.btn_export);
        mBtnRemoveClip = findViewById(R.id.btn_remove_clip);
        mBtnToggleParams = findViewById(R.id.btn_toggle_params);
        mExportParamsContainer = findViewById(R.id.export_params_container);
        mEtWidth = findViewById(R.id.et_width);
        mEtHeight = findViewById(R.id.et_height);
        mEtBitrate = findViewById(R.id.et_bitrate);

        mGLSurfaceView.setEGLContextClientVersion(3);
        mGLSurfaceView.setRenderer(this);
        mGLSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);

        mBtnPlay.setOnClickListener(this);
        mBtnPause.setOnClickListener(this);
        mBtnStop.setOnClickListener(this);
        mBtnAddVideo.setOnClickListener(this);
        mBtnAddWatermark.setOnClickListener(this);
        mBtnExport.setOnClickListener(this);
        mBtnRemoveClip.setOnClickListener(this);
        mBtnToggleParams.setOnClickListener(this);
        mSpCodecMode = findViewById(R.id.sp_codec_mode);
        mSpRenderFilter = findViewById(R.id.sp_render_filter);

        mEtWidth.setText("1920");
        mEtHeight.setText("1080");
        mEtBitrate.setText("15000000");

        String[] codecModes = {"软解软编", "硬解硬编"};
        ArrayAdapter<String> codecAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, codecModes);
        codecAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        mSpCodecMode.setAdapter(codecAdapter);

        String[] renderFilters = {"背景高斯", "黑白", "灵魂出窍"};
        ArrayAdapter<String> filterAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, renderFilters);
        filterAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        mSpRenderFilter.setAdapter(filterAdapter);

        mTimelineSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser && mTimelineEditor != null) {
                    long duration = mTimelineEditor.getTimelineDuration();
                    long position = (duration * progress) / 100;
                    mTimelineEditor.setCurrentPosition(position);
                    updateDurationText();

                    if (!mIsPlaying && mSurfaceCreated) {
                        mTimelineEditor.previewFrame(position);
                    }
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                mIsTouch = true;
                if (mIsPlaying) {
                    pausePreview();
                }
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                mIsTouch = false;
                if (mTimelineEditor != null && mSurfaceCreated) {
                    mTimelineEditor.seekToPosition(seekBar.getProgress());
                    mGLSurfaceView.requestRender();
                }
            }
        });

        updatePlayButtonState();
    }

    @Override
    public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
        Log.d(TAG, "onSurfaceCreated: Start");
        mSurfaceCreated = true;
        if (mTimelineEditor != null) {
            Log.d(TAG, "onSurfaceCreated: Calling native onSurfaceCreated");
            int result = mTimelineEditor.onSurfaceCreated(TimelineEditor.RENDER_TYPE_OPENGL);
            Log.d(TAG, "onSurfaceCreated: native result=" + result);
        }
        Log.d(TAG, "onSurfaceCreated: GLSurfaceView created");
    }

    @Override
    public void onSurfaceChanged(GL10 gl10, int w, int h) {
        Log.d(TAG, "onSurfaceChanged: called, w=" + w + ", h=" + h);
        if (mTimelineEditor != null) {
            Log.d(TAG, "onSurfaceChanged: Calling native onSurfaceChanged");
            mTimelineEditor.onSurfaceChanged(TimelineEditor.RENDER_TYPE_OPENGL, w, h);
            Log.d(TAG, "onSurfaceChanged: native call completed");
        }
    }

    @Override
    public void onDrawFrame(GL10 gl10) {
        Log.d(TAG, "onDrawFrame: called, mSurfaceCreated=" + mSurfaceCreated);
        if (mTimelineEditor != null && mSurfaceCreated) {
            Log.d(TAG, "onDrawFrame: Checking needRender");
            if (mTimelineEditor.checkAndClearNeedRender()) {
                Log.d(TAG, "onDrawFrame: Need render, calling native onDrawFrame");
                mTimelineEditor.onDrawFrame(TimelineEditor.RENDER_TYPE_OPENGL);
            } else {
                Log.d(TAG, "onDrawFrame: No need render");
            }
            Log.d(TAG, "onDrawFrame: native call completed");
        } else {
            Log.d(TAG, "onDrawFrame: skipped - mTimelineEditor=" + (mTimelineEditor != null) + ", mSurfaceCreated=" + mSurfaceCreated);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(TAG, "onResume: GLSurfaceView = " + mGLSurfaceView);
        if (mGLSurfaceView != null) {
            Log.d(TAG, "onResume: Calling GLSurfaceView.onResume()");
            mGLSurfaceView.onResume();
            Log.d(TAG, "onResume: GLSurfaceView.onResume() completed");
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mGLSurfaceView != null) {
            mGLSurfaceView.onPause();
        }
        if (mIsPlaying) {
            pausePreview();
        }
    }

    private void checkPermissions() {
        List<String> missingPermissions = new ArrayList<>();
        for (String permission : REQUIRED_PERMISSIONS) {
            if (ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                missingPermissions.add(permission);
            }
        }

        if (!missingPermissions.isEmpty()) {
            ActivityCompat.requestPermissions(this,
                    missingPermissions.toArray(new String[0]),
                    PERMISSION_REQUEST_CODE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_REQUEST_CODE) {
            boolean allGranted = true;
            for (int result : grantResults) {
                if (result != PackageManager.PERMISSION_GRANTED) {
                    allGranted = false;
                    break;
                }
            }
            if (!allGranted) {
                Toast.makeText(this, "需要存储权限才能使用此功能", Toast.LENGTH_LONG).show();
            }
        }
    }

    private void initTimelineEditor() {
        mTimelineEditor = new TimelineEditor();
        mTimelineEditor.init();
        mTimelineEditor.createTimeline(1920, 1080, 30);
        mTimelineEditor.addEventCallback(this);
    }

    @Override
    public void onClick(View v) {
        int id = v.getId();
        if (id == R.id.btn_play) {
            if (mTimelineEditor == null) {
                Toast.makeText(this, "编辑器未初始化", Toast.LENGTH_SHORT).show();
                return;
            }
            if (mTimelineEditor.isPreviewPaused()) {
                resumePreview();
            } else {
                startPreview();
            }
        } else if (id == R.id.btn_pause) {
            pausePreview();
        } else if (id == R.id.btn_stop) {
            stopPreview();
        } else if (id == R.id.btn_add_video) {
            selectVideos();
        } else if (id == R.id.btn_add_watermark) {
            selectWatermark();
        } else if (id == R.id.btn_export) {
            startExport();
        } else if (id == R.id.btn_remove_clip) {
            showRemoveClipDialog();
        } else if (id == R.id.btn_toggle_params) {
            toggleExportParams();
        }
    }

    private void toggleExportParams() {
        mParamsExpanded = !mParamsExpanded;
        if (mParamsExpanded) {
            mExportParamsContainer.setVisibility(View.VISIBLE);
            mBtnToggleParams.setText("导出参数设置 ▲");
        } else {
            mExportParamsContainer.setVisibility(View.GONE);
            mBtnToggleParams.setText("导出参数设置 ▼");
        }
    }

    private void startPreview() {
        if (mTimelineEditor == null) {
            Toast.makeText(this, "编辑器未初始化", Toast.LENGTH_SHORT).show();
            return;
        }
        
        if (!mSurfaceCreated) {
            Toast.makeText(this, "预览窗口未准备好，请稍候...", Toast.LENGTH_SHORT).show();
            return;
        }

        if (mSelectedVideos.isEmpty()) {
            Toast.makeText(this, "请先添加视频片段", Toast.LENGTH_SHORT).show();
            return;
        }

        if (mIsPlaying) {
            return;
        }

        Log.d(TAG, "startPreview: mSurfaceCreated=" + mSurfaceCreated);
        int result = mTimelineEditor.startPreview();
        Log.d(TAG, "startPreview: result=" + result);
        if (result == 0) {
            mIsPlaying = true;
            updatePlayButtonState();
            mGLSurfaceView.requestRender();
            Toast.makeText(this, "开始预览", Toast.LENGTH_SHORT).show();
        } else {
            Log.e(TAG, "startPreview failed: " + result);
        }
    }

    private void pausePreview() {
        if (mTimelineEditor != null && mIsPlaying) {
            mTimelineEditor.pausePreview();
            mIsPlaying = false;
            updatePlayButtonState();
        }
    }

    private void resumePreview() {
        if (mTimelineEditor != null && !mIsPlaying && mSurfaceCreated) {
            mTimelineEditor.resumePreview();
            mIsPlaying = true;
            updatePlayButtonState();
            mGLSurfaceView.requestRender();
        }
    }

    private void stopPreview() {
        if (mTimelineEditor != null) {
            mTimelineEditor.stopPreview();
            mIsPlaying = false;
            mTimelineSeekBar.setProgress(0);
            updatePlayButtonState();
        }
    }

    private void updatePlayButtonState() {
        if (mIsPlaying) {
            mBtnPlay.setVisibility(View.GONE);
            mBtnPause.setVisibility(View.VISIBLE);
        } else {
            mBtnPlay.setVisibility(View.VISIBLE);
            mBtnPause.setVisibility(View.GONE);
        }
    }

    private void selectVideos() {
        showVideoPickerDialog();
    }

    private void showVideoPickerDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("选择视频");

        String[] videoPaths = getVideoFiles();
        if (videoPaths == null || videoPaths.length == 0) {
            Toast.makeText(this, "未找到视频文件", Toast.LENGTH_SHORT).show();
            return;
        }

        boolean[] checkedItems = new boolean[videoPaths.length];
        builder.setMultiChoiceItems(videoPaths, checkedItems, (dialog, which, isChecked) -> {
            checkedItems[which] = isChecked;
        });

        builder.setPositiveButton("添加", (dialog, which) -> {
            for (int i = 0; i < videoPaths.length; i++) {
                if (checkedItems[i]) {
                    addVideoToTimeline(videoPaths[i]);
                }
            }
            updateClipList();
            updateDurationText();
        });

        builder.setNegativeButton("取消", null);
        builder.show();
    }

    private String[] getVideoFiles() {
        List<String> videos = new ArrayList<>();
        android.net.Uri collection = MediaStore.Video.Media.EXTERNAL_CONTENT_URI;
        String[] projection = {MediaStore.Video.Media.DATA, MediaStore.Video.Media.DISPLAY_NAME};

        try (Cursor cursor = getContentResolver().query(collection, projection, null, null, null)) {
            if (cursor != null) {
                int pathColumn = cursor.getColumnIndexOrThrow(MediaStore.Video.Media.DATA);
                while (cursor.moveToNext()) {
                    String path = cursor.getString(pathColumn);
                    if (path != null && new File(path).exists()) {
                        videos.add(path);
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error querying videos: " + e.getMessage());
        }

        return videos.toArray(new String[0]);
    }

    private void addVideoToTimeline(String videoPath) {
        if (mTimelineEditor != null) {
            int index = mTimelineEditor.addVideoClip(videoPath);
            if (index >= 0) {
                mSelectedVideos.add(videoPath);
                Log.d(TAG, "Added video: " + videoPath + " at index " + index);
            } else {
                Toast.makeText(this, "添加视频失败: " + videoPath, Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void selectWatermark() {
        androidx.appcompat.app.AlertDialog.Builder builder = new androidx.appcompat.app.AlertDialog.Builder(this);
        builder.setTitle("添加水印");

        final EditText input = new EditText(this);
        input.setHint("/sdcard/DCIM/watermark.png");
        builder.setView(input);

        builder.setPositiveButton("确定", (dialog, which) -> {
            String path = input.getText().toString().trim();
            if (!path.isEmpty()) {
                setWatermark(path);
            }
        });
        builder.setNegativeButton("取消", null);
        builder.show();
    }

    private void setWatermark(String path) {
        if (mTimelineEditor == null) return;

        int result = mTimelineEditor.setWatermark(path, 0, 1.0f, 0.2f);
        if (result == 0) {
            mWatermarkPath = path;
            Toast.makeText(this, "水印设置成功", Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, "水印设置失败: " + result, Toast.LENGTH_SHORT).show();
        }
    }

    private void updateClipList() {
        mClipListLayout.removeAllViews();
        int count = mTimelineEditor != null ? mTimelineEditor.getClipCount() : 0;

        for (int i = 0; i < count; i++) {
            TextView textView = new TextView(this);
            textView.setText("片段 " + (i + 1) + ": " + mSelectedVideos.get(i));
            textView.setPadding(16, 8, 16, 8);
            mClipListLayout.addView(textView);
        }
    }

    private void updateDurationText() {
        if (mTimelineEditor == null) return;

        long duration = mTimelineEditor.getTimelineDuration();
        long currentPos = mTimelineEditor.getCurrentPosition();

        long durationSec = duration / 1000;
        long currentSec = currentPos / 1000;

        mDurationText.setText(String.format("时长: %02d:%02d", durationSec / 60, durationSec % 60));
        mProgressText.setText(String.format("位置: %02d:%02d", currentSec / 60, currentSec % 60));

        if (duration > 0) {
            mTimelineSeekBar.setProgress((int) ((currentPos * 100) / duration));
        }
    }

    private void showRemoveClipDialog() {
        if (mTimelineEditor == null || mTimelineEditor.getClipCount() == 0) {
            Toast.makeText(this, "没有可删除的片段", Toast.LENGTH_SHORT).show();
            return;
        }

        String[] items = new String[mTimelineEditor.getClipCount()];
        for (int i = 0; i < items.length; i++) {
            items[i] = "片段 " + (i + 1);
        }

        new androidx.appcompat.app.AlertDialog.Builder(this)
                .setTitle("选择要删除的片段")
                .setItems(items, (dialog, which) -> {
                    if (mTimelineEditor != null) {
                        mTimelineEditor.removeVideoClip(which);
                        mSelectedVideos.remove(which);
                        updateClipList();
                        updateDurationText();
                    }
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void startExport() {
        if (mTimelineEditor == null || mTimelineEditor.getClipCount() == 0) {
            Toast.makeText(this, "请先添加视频片段", Toast.LENGTH_SHORT).show();
            return;
        }

        int width = 1920;
        int height = 1080;
        int bitrate = 15000000;

        try {
            width = Integer.parseInt(mEtWidth.getText().toString());
            height = Integer.parseInt(mEtHeight.getText().toString());
            bitrate = Integer.parseInt(mEtBitrate.getText().toString());
        } catch (NumberFormatException e) {
            Log.e(TAG, "Invalid export params");
        }

        mTimelineEditor.setOutputPath("/sdcard/DCIM/export_" + System.currentTimeMillis() + ".mp4");
        mTimelineEditor.setExportParams(width, height, bitrate);

        mProgressDialog = new ProgressDialog(this);
        mProgressDialog.setTitle("导出中");
        mProgressDialog.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
        mProgressDialog.setMax(100);
        mProgressDialog.show();

        new Thread(() -> {
            int result = mTimelineEditor.startExport();
            runOnUiThread(() -> {
                if (mProgressDialog != null) {
                    mProgressDialog.dismiss();
                    mProgressDialog = null;
                }
                if (result == 0) {
                    Toast.makeText(this, "导出成功", Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(this, "导出失败: " + result, Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }

    @Override
    public void onEditorEvent(int msgType, float msgValue) {
        runOnUiThread(() -> {
            switch (msgType) {
                case TimelineEditor.MSG_PREVIEW_POSITION:
                    if (!mIsTouch) {
                        mTimelineSeekBar.setProgress((int) msgValue);
                    }
                    updateDurationText();
                    if (mIsPlaying && mGLSurfaceView != null) {
                        mGLSurfaceView.requestRender();
                    }
                    break;
                case TimelineEditor.MSG_EXPORT_PROGRESS:
                    if (mProgressDialog != null) {
                        mProgressDialog.setProgress((int) msgValue);
                    }
                    break;
                case TimelineEditor.MSG_EXPORT_COMPLETE:
                    if (mProgressDialog != null) {
                        mProgressDialog.dismiss();
                        mProgressDialog = null;
                    }
                    Toast.makeText(this, "导出完成", Toast.LENGTH_SHORT).show();
                    break;
                case TimelineEditor.MSG_EXPORT_ERROR:
                    if (mProgressDialog != null) {
                        mProgressDialog.dismiss();
                        mProgressDialog = null;
                    }
                    Toast.makeText(this, "导出错误: " + (int) msgValue, Toast.LENGTH_SHORT).show();
                    break;
            }
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        Log.d(TAG, "onDestroy: Stopping preview");
        if (mTimelineEditor != null) {
            mTimelineEditor.stopPreview();
            Log.d(TAG, "onDestroy: Preview stopped");
            mTimelineEditor.unInit();
            Log.d(TAG, "onDestroy: TimelineEditor uninitialized");
        }
    }
}
