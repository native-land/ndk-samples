/*
 * Copyright 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.sample.echo;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;

/**
 * 音频回声效果主活动类
 * <p>
 * 这个类实现了一个音频回声效果演示应用，主要功能包括：
 * 1. 实时音频捕获和播放
 * 2. 可调节的回声延迟和衰减效果
 * 3. 使用 Android 快速音频路径实现低延迟
 * 4. 通过 JNI 调用原生 C++ 音频处理代码
 */
public class MainActivity extends Activity
        implements ActivityCompat.OnRequestPermissionsResultCallback {

    // 录音权限请求码
    private static final int AUDIO_ECHO_REQUEST = 0;

    // UI 控件
    private Button controlButton;     // 开始/停止回声的控制按钮
    private TextView statusView;       // 状态信息显示文本

    /**
     * 原生采样率
     */
    private String nativeSampleRate;

    /**
     * 原生缓冲区大小
     */
    private String nativeSampleBufSize;

    // 延迟控制相关
    private SeekBar delaySeekBar;      // 延迟时间调节滑动条
    private TextView curDelayTV;       // 当前延迟值显示

    /**
     * 回声延迟进度值（毫秒）
     */
    private int echoDelayProgress;

    // 衰减控制相关
    private SeekBar decaySeekBar;      // 衰减权重调节滑动条
    private TextView curDecayTV;       // 当前衰减值显示

    /**
     * 回声衰减进度值（0.0-1.0）
     */
    private float echoDecayProgress;

    // 状态变量
    private boolean supportRecording;  // 是否支持录音
    private Boolean isPlaying = false; // 是否正在播放回声效果

    /**
     * 活动创建时的初始化方法
     *
     * @param savedInstanceState 保存的实例状态
     */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 初始化UI控件
        controlButton = (Button) findViewById((R.id.capture_control_button));
        statusView = (TextView) findViewById(R.id.statusView);

        // 查询设备的原生音频参数
        queryNativeAudioParameters();

        // 初始化延迟控制滑动条
        delaySeekBar = (SeekBar) findViewById(R.id.delaySeekBar);
        curDelayTV = (TextView) findViewById(R.id.curDelay);

        // 计算初始延迟值（转换为毫秒）
        echoDelayProgress = delaySeekBar.getProgress() * 1000 / delaySeekBar.getMax();

        // 设置延迟滑动条的监听器
        delaySeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                // 计算当前延迟值（0.0-1.0）
                float curVal = (float) progress / delaySeekBar.getMax();
                curDelayTV.setText(String.format("%s", curVal));

                // 更新滑动条标签位置
                setSeekBarPromptPosition(delaySeekBar, curDelayTV);

                // 只有用户操作时才更新配置
                if (!fromUser) return;

                // 将进度值转换为毫秒并配置回声效果
                echoDelayProgress = progress * 1000 / delaySeekBar.getMax();
                configureEcho(echoDelayProgress, echoDecayProgress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        // 设置延迟标签的初始位置
        delaySeekBar.post(new Runnable() {
            @Override
            public void run() {
                setSeekBarPromptPosition(delaySeekBar, curDelayTV);
            }
        });

        // 初始化衰减控制滑动条
        decaySeekBar = (SeekBar) findViewById(R.id.decaySeekBar);
        curDecayTV = (TextView) findViewById(R.id.curDecay);

        // 计算初始衰减值（0.0-1.0）
        echoDecayProgress = (float) decaySeekBar.getProgress() / decaySeekBar.getMax();

        // 设置衰减滑动条的监听器
        decaySeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                // 计算当前衰减值（0.0-1.0）
                float curVal = (float) progress / seekBar.getMax();
                curDecayTV.setText(String.format("%s", curVal));

                // 更新滑动条标签位置
                setSeekBarPromptPosition(decaySeekBar, curDecayTV);

                // 只有用户操作时才更新配置
                if (!fromUser)
                    return;

                // 更新衰减值并配置回声效果
                echoDecayProgress = curVal;
                configureEcho(echoDelayProgress, echoDecayProgress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        // 设置衰减标签的初始位置
        decaySeekBar.post(new Runnable() {
            @Override
            public void run() {
                setSeekBarPromptPosition(decaySeekBar, curDecayTV);
            }
        });

        // 初始化原生音频系统UI
        updateNativeAudioUI();

        // 如果支持录音，则创建音频引擎
        if (supportRecording) {
            createSLEngine(
                    Integer.parseInt(nativeSampleRate),
                    Integer.parseInt(nativeSampleBufSize),
                    echoDelayProgress,
                    echoDecayProgress);
        }
    }

    /**
     * 设置滑动条标签的位置
     * 根据滑动条的进度值动态调整标签位置，使其跟随滑动条的滑块
     *
     * @param seekBar 滑动条控件
     * @param label   标签文本控件
     */
    private void setSeekBarPromptPosition(SeekBar seekBar, TextView label) {
        // 计算滑块的X坐标位置
        float thumbX = (float) seekBar.getProgress() / seekBar.getMax() *
                seekBar.getWidth() + seekBar.getX();
        // 设置标签位置，使其居中对齐滑块
        label.setX(thumbX - label.getWidth() / 2.0f);
    }

    /**
     * 活动销毁时的清理方法
     */
    @Override
    protected void onDestroy() {
        if (supportRecording) {
            // 如果正在播放，先停止播放
            if (isPlaying) {
                stopPlay();
            }
            // 删除音频引擎，释放资源
            deleteSLEngine();
            isPlaying = false;
        }
        super.onDestroy();
    }

    /**
     * 创建选项菜单
     *
     * @param menu 菜单对象
     * @return 是否成功创建菜单
     */
    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // 加载菜单资源，将菜单项添加到操作栏
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    /**
     * 处理选项菜单项选择事件
     *
     * @param item 被选择的菜单项
     * @return 是否成功处理该菜单项
     */
    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // 获取菜单项的ID
        int id = item.getItemId();

        // 处理设置菜单项
        if (id == R.id.action_settings) {
            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    /**
     * 开始或停止回声效果
     * <p>
     * 这个方法控制整个音频回声系统的启动和停止，包括：
     * 1. 创建音频播放器和录制器
     * 2. 启动音频处理流程
     * 3. 更新UI状态
     */
    private void startEcho() {
        // 检查是否支持录音功能
        if (!supportRecording) {
            return;
        }

        if (!isPlaying) {
            // 开始回声效果

            // 创建音频播放器
            if (!createSLBufferQueueAudioPlayer()) {
                statusView.setText(getString(R.string.player_error_msg));
                return;
            }

            // 创建音频录制器
            if (!createAudioRecorder()) {
                // 如果录制器创建失败，清理播放器
                deleteSLBufferQueueAudioPlayer();
                statusView.setText(getString(R.string.recorder_error_msg));
                return;
            }

            // 启动播放（这会触发录制开始）
            startPlay();
            statusView.setText(getString(R.string.echoing_status_msg));
        } else {
            // 停止回声效果

            // 停止播放（这会触发录制停止）
            stopPlay();

            // 更新UI显示
            updateNativeAudioUI();

            // 清理音频资源
            deleteAudioRecorder();
            deleteSLBufferQueueAudioPlayer();
        }

        // 切换播放状态
        isPlaying = !isPlaying;

        // 更新按钮文本
        controlButton.setText(getString(isPlaying ?
                R.string.cmd_stop_echo : R.string.cmd_start_echo));
    }

    /**
     * 回声按钮点击事件处理
     *
     * @param view 被点击的视图
     */
    public void onEchoClick(View view) {
        // 检查录音权限
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) !=
                PackageManager.PERMISSION_GRANTED) {
            // 如果没有权限，请求权限
            statusView.setText(getString(R.string.request_permission_status_msg));
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{Manifest.permission.RECORD_AUDIO},
                    AUDIO_ECHO_REQUEST);
            return;
        }

        // 有权限则开始回声效果
        startEcho();
    }

    /**
     * 获取低延迟参数按钮点击事件处理
     *
     * @param view 被点击的视图
     */
    public void getLowLatencyParameters(View view) {
        // 更新并显示原生音频参数
        updateNativeAudioUI();
    }

    /**
     * 查询设备的原生音频参数
     * <p>
     * 这个方法获取设备支持的最佳音频参数，包括：
     * 1. 采样率
     * 2. 缓冲区大小
     * 3. 录音支持情况
     */
    private void queryNativeAudioParameters() {
        supportRecording = true;

        // 获取音频管理器
        AudioManager myAudioMgr = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (myAudioMgr == null) {
            supportRecording = false;
            return;
        }

        // 获取推荐的输出采样率
        nativeSampleRate = myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);

        // 获取推荐的输出缓冲区大小（帧数）
        nativeSampleBufSize = myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);

        // 检查录音缓冲区大小
        // 注意：这里硬编码为单声道，C++和Java两端都是如此
        int recBufSize = AudioRecord.getMinBufferSize(
                Integer.parseInt(nativeSampleRate),
                AudioFormat.CHANNEL_IN_MONO,           // 单声道输入
                AudioFormat.ENCODING_PCM_16BIT);       // 16位PCM编码

        // 检查是否能够获取有效的录音缓冲区大小
        if (recBufSize == AudioRecord.ERROR ||
                recBufSize == AudioRecord.ERROR_BAD_VALUE) {
            supportRecording = false;
        }
    }

    /**
     * 更新原生音频UI显示
     * <p>
     * 根据设备的音频支持情况更新状态文本和按钮状态
     */
    private void updateNativeAudioUI() {
        if (!supportRecording) {
            // 如果不支持录音，显示错误信息并禁用按钮
            statusView.setText(getString(R.string.mic_error_msg));
            controlButton.setEnabled(false);
            return;
        }

        // 显示快速音频路径的参数信息
        statusView.setText(getString(R.string.fast_audio_info_msg,
                nativeSampleRate, nativeSampleBufSize));
    }

    /**
     * 权限请求结果处理
     *
     * @param requestCode  请求码
     * @param permissions  请求的权限数组
     * @param grantResults 权限授予结果数组
     */
    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        // 检查是否是我们的录音权限请求
        if (AUDIO_ECHO_REQUEST != requestCode) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }

        // 检查权限是否被授予
        if (grantResults.length != 1 ||
                grantResults[0] != PackageManager.PERMISSION_GRANTED) {
            /*
             * 当用户拒绝权限时，显示Toast提示RECORD_AUDIO权限是必需的
             * 同时在UI上显示状态信息
             * 应用会回到原始状态，用户需要重新点击"开始"按钮重试
             */
            statusView.setText(getString(R.string.permission_error_msg));
            Toast.makeText(getApplicationContext(),
                    getString(R.string.permission_prompt_msg),
                    Toast.LENGTH_SHORT).show();
            return;
        }

        /*
         * 当权限被授予时，提示用户状态。用户需要重新点击"开始"按钮
         * 来执行正常操作。这样可以避免在代码中处理按钮监听器的异步逻辑
         */
        statusView.setText(getString(R.string.permission_granted_msg, getString(R.string.cmd_start_echo)));

        // 回调在应用线程上运行，因此可以安全地恢复操作
        startEcho();
    }

    /*
     * 加载原生库
     */
    static {
        System.loadLibrary("echo");
    }

    /*
     * JNI函数声明
     * 这些函数在原生C++代码中实现，用于音频处理
     */

    /**
     * 创建OpenSL ES音频引擎
     *
     * @param rate         采样率
     * @param framesPerBuf 每个缓冲区的帧数
     * @param delayInMs    延迟时间（毫秒）
     * @param decay        衰减系数
     */
    static native void createSLEngine(int rate, int framesPerBuf,
                                      long delayInMs, float decay);

    /**
     * 删除OpenSL ES音频引擎
     */
    static native void deleteSLEngine();

    /**
     * 配置回声效果参数
     *
     * @param delayInMs 延迟时间（毫秒）
     * @param decay     衰减系数
     * @return 是否配置成功
     */
    static native boolean configureEcho(int delayInMs, float decay);

    /**
     * 创建音频播放器
     *
     * @return 是否创建成功
     */
    static native boolean createSLBufferQueueAudioPlayer();

    /**
     * 删除音频播放器
     */
    static native void deleteSLBufferQueueAudioPlayer();

    /**
     * 创建音频录制器
     *
     * @return 是否创建成功
     */
    static native boolean createAudioRecorder();

    /**
     * 删除音频录制器
     */
    static native void deleteAudioRecorder();

    /**
     * 开始播放音频
     */
    static native void startPlay();

    /**
     * 停止播放音频
     */
    static native void stopPlay();
}
