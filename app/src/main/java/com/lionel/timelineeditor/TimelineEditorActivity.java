package com.lionel.timelineeditor;

import android.Manifest;
import android.app.ProgressDialog;
import android.content.ContentUris;
import android.content.Context;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class TimelineEditorActivity extends AppCompatActivity implements View.OnClickListener {
    private static final String TAG = "TimelineEditorActivity";
    private static final int PERMISSION_REQUEST_CODE = 1;
    private static final int REQUEST_CODE_SELECT_VIDEOS = 100;
    private static final int REQUEST_CODE_SELECT_WATERMARK = 101;

    private static final String[] REQUIRED_PERMISSIONS = {
            Manifest.permission.READ_EXTERNAL_STORAGE,
            Manifest.permission.WRITE_EXTERNAL_STORAGE
    };

    private TimelineEditor mTimelineEditor;
    private LinearLayout mClipListLayout;
    private TextView mDurationText;
    private TextView mProgressText;
    private SeekBar mTimelineSeekBar;
    private Button mBtnAddVideo;
    private Button mBtnAddWatermark;
    private Button mBtnExport;
    private Button mBtnRemoveClip;
    private EditText mEtWidth;
    private EditText mEtHeight;
    private EditText mEtBitrate;
    private ProgressDialog mProgressDialog;

    private List<String> mSelectedVideos = new ArrayList<>();
    private String mWatermarkPath;
    private int mSelectedClipIndex = -1;

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
        mBtnAddVideo = findViewById(R.id.btn_add_video);
        mBtnAddWatermark = findViewById(R.id.btn_add_watermark);
        mBtnExport = findViewById(R.id.btn_export);
        mBtnRemoveClip = findViewById(R.id.btn_remove_clip);
        mEtWidth = findViewById(R.id.et_width);
        mEtHeight = findViewById(R.id.et_height);
        mEtBitrate = findViewById(R.id.et_bitrate);

        mBtnAddVideo.setOnClickListener(this);
        mBtnAddWatermark.setOnClickListener(this);
        mBtnExport.setOnClickListener(this);
        mBtnRemoveClip.setOnClickListener(this);

        // 设置默认值
        mEtWidth.setText("1920");
        mEtHeight.setText("1080");
        mEtBitrate.setText("15000000");

        mTimelineSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser && mTimelineEditor != null) {
                    long duration = mTimelineEditor.getTimelineDuration();
                    long position = (duration * progress) / 100;
                    mTimelineEditor.setCurrentPosition(position);
                    updateDurationText();
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });
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
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
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
        mTimelineEditor.addEventCallback(new TimelineEditor.EventCallback() {
            @Override
            public void onEditorEvent(int msgType, float msgValue) {
                mHandler.post(() -> handleEditorEvent(msgType, msgValue));
            }
        });
    }

    @Override
    public void onClick(View v) {
        int id = v.getId();
        if (id == R.id.btn_add_video) {
            selectVideos();
        } else if (id == R.id.btn_add_watermark) {
            selectWatermark();
        } else if (id == R.id.btn_export) {
            startExport();
        } else if (id == R.id.btn_remove_clip) {
            showRemoveClipDialog();
        }
    }

    private void selectVideos() {
        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission("android.permission.READ_MEDIA_VIDEO") != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{"android.permission.READ_MEDIA_VIDEO"}, REQUEST_CODE_SELECT_VIDEOS);
                return;
            }
        }

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
        
        // 从 /sdcard/video/ 文件夹读取视频文件
        File videoDir = new File(Environment.getExternalStorageDirectory(), "video");
        if (!videoDir.exists()) {
            Log.w(TAG, "Video directory does not exist: " + videoDir.getAbsolutePath());
            // 尝试创建目录
            if (videoDir.mkdirs()) {
                Log.i(TAG, "Created video directory: " + videoDir.getAbsolutePath());
            }
            return videos.toArray(new String[0]);
        }
        
        if (!videoDir.isDirectory()) {
            Log.e(TAG, "Video path is not a directory: " + videoDir.getAbsolutePath());
            return videos.toArray(new String[0]);
        }
        
        // 获取视频文件（支持常见视频格式）
        File[] files = videoDir.listFiles((dir, name) -> {
            String lowerName = name.toLowerCase();
            return lowerName.endsWith(".mp4") || 
                   lowerName.endsWith(".avi") || 
                   lowerName.endsWith(".mkv") || 
                   lowerName.endsWith(".mov") || 
                   lowerName.endsWith(".flv") ||
                   lowerName.endsWith(".wmv") ||
                   lowerName.endsWith(".3gp") ||
                   lowerName.endsWith(".ts");
        });
        
        if (files != null) {
            for (File file : files) {
                if (file.exists() && file.canRead()) {
                    videos.add(file.getAbsolutePath());
                    Log.d(TAG, "Found video: " + file.getAbsolutePath());
                }
            }
        }
        
        Log.i(TAG, "Found " + videos.size() + " videos in " + videoDir.getAbsolutePath());
        return videos.toArray(new String[0]);
    }

    private void addVideoToTimeline(String videoPath) {
        if (mTimelineEditor != null) {
            int index = mTimelineEditor.addVideoClip(videoPath);
            if (index >= 0) {
                mSelectedVideos.add(videoPath);
                Log.d(TAG, "Added video: " + videoPath + " at index " + index);
            } else {
                Toast.makeText(this, "添加视频失败：" + videoPath, Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void updateClipList() {
        mClipListLayout.removeAllViews();
        
        for (int i = 0; i < mSelectedVideos.size(); i++) {
            String videoPath = mSelectedVideos.get(i);
            TextView clipView = new TextView(this);
            File file = new File(videoPath);
            clipView.setText(String.format(Locale.getDefault(), "片段 %d: %s", i + 1, file.getName()));
            clipView.setPadding(16, 16, 16, 16);
            clipView.setBackgroundColor(ContextCompat.getColor(this, 
                    i == mSelectedClipIndex ? android.R.color.darker_gray : android.R.color.transparent));
            
            final int index = i;
            clipView.setOnClickListener(v -> {
                mSelectedClipIndex = index;
                updateClipList();
            });
            
            mClipListLayout.addView(clipView);
        }
    }

    private void updateDurationText() {
        if (mTimelineEditor != null) {
            long durationMs = mTimelineEditor.getTimelineDuration();
            long positionMs = mTimelineEditor.getCurrentPosition();
            
            SimpleDateFormat sdf = new SimpleDateFormat("mm:ss", Locale.getDefault());
            String posStr = sdf.format(new Date(positionMs));
            String durStr = sdf.format(new Date(durationMs));
            
            mDurationText.setText(String.format("%s / %s", posStr, durStr));
            
            if (durationMs > 0) {
                int progress = (int) ((positionMs * 100) / durationMs);
                mTimelineSeekBar.setProgress(progress);
            }
        }
    }

    private void selectWatermark() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("选择水印");
        
        String[] options = {"使用默认水印", "从设备选择图片"};
        
        builder.setItems(options, (dialog, which) -> {
            if (which == 0) {
                setupDefaultWatermark();
            } else {
                selectImageFromDevice();
            }
        });

        builder.setNegativeButton("取消", null);
        builder.show();
    }
    
    private void setupDefaultWatermark() {
        String watermarkPath = copyAssetToCache("watermark/demo.png");
        if (watermarkPath == null) {
            watermarkPath = createTextWatermark();
        }
        
        if (watermarkPath != null) {
            mWatermarkPath = watermarkPath;
            setWatermark();
            Toast.makeText(this, "已设置默认水印", Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, "设置水印失败", Toast.LENGTH_SHORT).show();
        }
    }
    
    private String copyAssetToCache(String assetPath) {
        try {
            InputStream is = getAssets().open(assetPath);
            File outFile = new File(getCacheDir(), "watermark.png");
            FileOutputStream fos = new FileOutputStream(outFile);
            byte[] buffer = new byte[1024];
            int read;
            while ((read = is.read(buffer)) != -1) {
                fos.write(buffer, 0, read);
            }
            is.close();
            fos.close();
            Log.d(TAG, "Copied asset to: " + outFile.getAbsolutePath());
            return outFile.getAbsolutePath();
        } catch (IOException e) {
            Log.e(TAG, "Failed to copy asset: " + e.getMessage());
            return null;
        }
    }
    
    private String createTextWatermark() {
        return null;
    }
    
    private void selectImageFromDevice() {
        String[] imagePaths = getImageFiles();
        if (imagePaths == null || imagePaths.length == 0) {
            Toast.makeText(this, "设备上未找到图片文件", Toast.LENGTH_SHORT).show();
            return;
        }

        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("选择图片");
        builder.setItems(imagePaths, (dialog, which) -> {
            mWatermarkPath = imagePaths[which];
            setWatermark();
        });
        builder.setNegativeButton("取消", null);
        builder.show();
    }

    private String[] getImageFiles() {
        List<String> images = new ArrayList<>();
        Uri collection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
        String[] projection = {MediaStore.Images.Media.DATA};
        
        try (Cursor cursor = getContentResolver().query(collection, projection, null, null, null)) {
            if (cursor != null) {
                int pathColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATA);
                while (cursor.moveToNext()) {
                    String path = cursor.getString(pathColumn);
                    if (path != null && new File(path).exists()) {
                        images.add(path);
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error querying images: " + e.getMessage());
        }

        return images.toArray(new String[0]);
    }

    private void setWatermark() {
        if (mTimelineEditor != null && mWatermarkPath != null) {
            mTimelineEditor.setWatermark(mWatermarkPath, 
                    TimelineEditor.WATERMARK_POSITION_BOTTOM_RIGHT, 
                    0.8f, 0.2f);
            Toast.makeText(this, "水印已设置", Toast.LENGTH_SHORT).show();
        }
    }

    private void showRemoveClipDialog() {
        if (mSelectedVideos.isEmpty()) {
            Toast.makeText(this, "没有可删除的片段", Toast.LENGTH_SHORT).show();
            return;
        }

        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("选择要删除的片段");
        
        String[] clipNames = new String[mSelectedVideos.size()];
        for (int i = 0; i < mSelectedVideos.size(); i++) {
            clipNames[i] = String.format(Locale.getDefault(), "片段 %d", i + 1);
        }

        builder.setItems(clipNames, (dialog, which) -> {
            removeClip(which);
        });

        builder.setNegativeButton("取消", null);
        builder.show();
    }

    private void removeClip(int index) {
        if (mTimelineEditor != null && index >= 0 && index < mSelectedVideos.size()) {
            mTimelineEditor.removeVideoClip(index);
            mSelectedVideos.remove(index);
            updateClipList();
            updateDurationText();
            Toast.makeText(this, "片段已删除", Toast.LENGTH_SHORT).show();
        }
    }

    private void startExport() {
        if (mSelectedVideos.isEmpty()) {
            Toast.makeText(this, "请先添加视频片段", Toast.LENGTH_SHORT).show();
            return;
        }

        // 获取导出参数
        int width = 1920;
        int height = 1080;
        int bitrate = 15000000;
        
        try {
            width = Integer.parseInt(mEtWidth.getText().toString().trim());
            height = Integer.parseInt(mEtHeight.getText().toString().trim());
            bitrate = Integer.parseInt(mEtBitrate.getText().toString().trim());
        } catch (NumberFormatException e) {
            Toast.makeText(this, "参数格式错误，使用默认值", Toast.LENGTH_SHORT).show();
        }
        
        // 验证参数
        if (width <= 0 || height <= 0 || bitrate <= 0) {
            Toast.makeText(this, "参数必须大于 0", Toast.LENGTH_SHORT).show();
            return;
        }

        String outputPath = generateOutputPath();
        mTimelineEditor.setOutputPath(outputPath);
        mTimelineEditor.setExportParams(width, height, bitrate);
        
        mTimelineEditor.setHistogramEqualization(true, 0.8f);

        mProgressDialog = new ProgressDialog(this);
        mProgressDialog.setTitle("导出中");
        mProgressDialog.setMessage("正在导出视频...\n分辨率：" + width + "x" + height + "\n码率：" + bitrate + " bps");
        mProgressDialog.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
        mProgressDialog.setMax(100);
        mProgressDialog.setCancelable(false);
        mProgressDialog.show();

        int result = mTimelineEditor.startExport();
        if (result != 0) {
            mProgressDialog.dismiss();
            Toast.makeText(this, "导出启动失败", Toast.LENGTH_SHORT).show();
        }
    }

    private String generateOutputPath() {
        File outputDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES);
        if (!outputDir.exists()) {
            outputDir.mkdirs();
        }
        
        SimpleDateFormat sdf = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault());
        String fileName = "timeline_" + sdf.format(new Date()) + ".mp4";
        
        return new File(outputDir, fileName).getAbsolutePath();
    }

    private void handleEditorEvent(int msgType, float msgValue) {
        switch (msgType) {
            case TimelineEditor.MSG_EXPORT_PROGRESS:
                if (mProgressDialog != null) {
                    int progress = (int) msgValue;
                    mProgressDialog.setProgress(progress);
                    mProgressText.setText(String.format(Locale.getDefault(), "进度：%d%%", progress));
                }
                break;
                
            case TimelineEditor.MSG_EXPORT_COMPLETE:
                if (mProgressDialog != null) {
                    mProgressDialog.dismiss();
                }
                if (msgValue == 0) {
                    Toast.makeText(this, "导出完成!", Toast.LENGTH_LONG).show();
                } else {
                    Toast.makeText(this, "导出失败", Toast.LENGTH_SHORT).show();
                }
                break;
                
            case TimelineEditor.MSG_EXPORT_ERROR:
                if (mProgressDialog != null) {
                    mProgressDialog.dismiss();
                }
                Toast.makeText(this, "导出错误", Toast.LENGTH_SHORT).show();
                break;
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mTimelineEditor != null) {
            if (mTimelineEditor.isExporting()) {
                mTimelineEditor.stopExport();
            }
            mTimelineEditor.destroyTimeline();
            mTimelineEditor.unInit();
        }
        
        if (mProgressDialog != null) {
            mProgressDialog.dismiss();
        }
    }
}
