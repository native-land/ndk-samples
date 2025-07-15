/*
 * Copyright 2015 The Android Open Source Project
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

/**
 * 音频回声效果引擎主文件
 *
 * 本文件实现了一个基于 OpenSL ES 的音频回声效果引擎，用于演示 Android NDK
 * 中的音频处理功能。
 * 主要功能包括：音频录制、音频播放、实时音效处理、音频缓冲管理等。
 */

// C++ 头文件包含语法：#include 用于包含头文件
#include <SLES/OpenSLES_Android.h>  // OpenSL ES Android 扩展头文件，用于音频处理
#include <jni.h>                    // JNI 头文件，用于 Java 和 C++ 之间的接口
#include <sys/types.h>              // 系统类型定义头文件

// C++ 标准库头文件（使用 <c 前缀> 代替 C 语言的 .h 后缀）
#include <cassert>  // 断言宏定义
#include <cstring>  // 字符串操作函数（如 memset）

// 项目自定义头文件
#include "audio_common.h"    // 音频通用定义和常量
#include "audio_effect.h"    // 音频效果处理类
#include "audio_player.h"    // 音频播放器类
#include "audio_recorder.h"  // 音频录制器类
#include "jni_interface.h"   // JNI 接口定义

/**
 * 回声音频引擎结构体
 *
 * C++ 语法说明：
 * - struct 关键字定义结构体，类似于 class，但默认成员访问权限为 public
 * - 成员变量使用下划线后缀命名（Google C++ 风格）
 * - 指针类型使用 * 号表示，如 AudioRecorder *recorder_
 */
struct EchoAudioEngine {
  // 音频参数配置
  SLmilliHertz fastPathSampleRate_;  // 采样率（毫赫兹为单位，OpenSL ES 规范）
  uint32_t fastPathFramesPerBuf_;    // 每个缓冲区的音频帧数
  uint16_t sampleChannels_;          // 音频通道数（1=单声道，2=立体声）
  uint16_t bitsPerSample_;           // 每个样本的位数（通常为 16 位）

  // OpenSL ES 音频引擎对象
  SLObjectItf slEngineObj_;  // OpenSL ES 引擎对象接口（SLObjectItf 是 OpenSL ES
                             // 的基础接口类型）
  SLEngineItf slEngineItf_;  // OpenSL ES 引擎功能接口

  // 音频处理组件指针
  AudioRecorder* recorder_;   // 音频录制器对象指针
  AudioPlayer* player_;       // 音频播放器对象指针
  AudioQueue* freeBufQueue_;  // 空闲缓冲区队列指针（负责队列的内存管理）
  AudioQueue* recBufQueue_;   // 录制缓冲区队列指针（负责队列的内存管理）

  // 缓冲区管理
  sample_buf* bufs_;     // 音频缓冲区数组指针
  uint32_t bufCount_;    // 缓冲区总数量
  uint32_t frameCount_;  // 当前处理的音频帧计数器

  // 回声效果参数
  int64_t echoDelay_;        // 回声延迟时间（毫秒）
  float echoDecay_;          // 回声衰减系数（0.0-1.0，控制回声强度）
  AudioDelay* delayEffect_;  // 音频延迟效果处理器指针
};

/**
 * C++ 语法说明：static 关键字
 * - static 修饰全局变量，表示该变量只在当前文件内可见（内部链接）
 * - 避免了全局命名空间污染，是良好的 C++ 编程实践
 */
static EchoAudioEngine engine;  // 全局音频引擎实例（文件作用域）

/**
 * 函数声明语法：
 * - bool 是返回类型
 * - EngineService 是函数名
 * - 括号内是参数列表：void *ctx（通用指针），uint32_t msg（消息类型），void
 * *data（数据指针）
 * - 分号结尾表示这是函数声明，实际定义在文件后面
 */
bool EngineService(void* ctx, uint32_t msg, void* data);

/**
 * 创建 OpenSL ES 音频引擎
 *
 * JNI 函数命名规则：Java_<包名>_<类名>_<方法名>
 *
 * C++ 语法说明：
 * - JNIEXPORT: JNI 导出宏，表示该函数可被 Java 调用
 * - JNICALL: JNI 调用约定宏，定义函数调用方式
 * - JNIEnv *env: JNI 环境指针，用于与 Java 虚拟机交互
 * - jclass type: Java 类对象
 * - jint, jlong, jfloat: JNI 基本数据类型，对应 Java 的 int, long, float
 *
 * 业务逻辑：
 * 1. 初始化音频引擎参数
 * 2. 创建 OpenSL ES 引擎对象
 * 3. 分配音频缓冲区
 * 4. 初始化音频队列
 * 5. 创建音频延迟效果器
 */
JNIEXPORT void JNICALL Java_com_google_sample_echo_MainActivity_createSLEngine(
    JNIEnv* env, jclass type, jint sampleRate, jint framesPerBuf,
    jlong delayInMs, jfloat decay) {
  SLresult result;  // OpenSL ES 操作结果类型

  /**
   * C++ 语法：memset 函数
   * - 第一个参数：目标内存地址
   * - 第二个参数：填充值（0 表示清零）
   * - 第三个参数：填充字节数（sizeof 获取结构体大小）
   * - 作用：将 engine 结构体的所有成员初始化为 0
   */
  memset(&engine, 0, sizeof(engine));

  /**
   * C++ 类型转换语法：static_cast<目标类型>(源值)
   * - 这是 C++ 推荐的显式类型转换方式，比 C 风格转换更安全
   * - 这里将 Java 的 jint 转换为 OpenSL ES 的 SLmilliHertz 类型
   * - 乘以 1000 是因为 OpenSL ES 使用毫赫兹作为采样率单位
   */
  engine.fastPathSampleRate_ = static_cast<SLmilliHertz>(sampleRate) * 1000;
  engine.fastPathFramesPerBuf_ = static_cast<uint32_t>(framesPerBuf);
  engine.sampleChannels_ = AUDIO_SAMPLE_CHANNELS;  // 使用预定义的音频通道数常量
  engine.bitsPerSample_ = SL_PCMSAMPLEFORMAT_FIXED_16;  // 16 位固定点 PCM 格式

  /**
   * OpenSL ES API 调用：创建音频引擎
   * - slCreateEngine: OpenSL ES 全局函数，创建音频引擎对象
   * - 第一个参数：输出的引擎对象指针
   * - 后续参数：选项和接口配置（此处使用默认配置）
   */
  result = slCreateEngine(&engine.slEngineObj_, 0, NULL, 0, NULL, NULL);
  SLASSERT(result);  // 断言宏，检查操作是否成功

  /**
   * OpenSL ES 对象生命周期管理：
   * 1. Create: 创建对象
   * 2. Realize: 实现对象（分配资源）
   * 3. GetInterface: 获取功能接口
   * 4. Use: 使用对象
   * 5. Destroy: 销毁对象
   */
  result =
      (*engine.slEngineObj_)->Realize(engine.slEngineObj_, SL_BOOLEAN_FALSE);
  SLASSERT(result);

  /**
   * 获取引擎功能接口
   * - SL_IID_ENGINE: 引擎接口标识符
   * - &engine.slEngineItf_: 输出接口指针的地址
   */
  result = (*engine.slEngineObj_)
               ->GetInterface(engine.slEngineObj_, SL_IID_ENGINE,
                              &engine.slEngineItf_);
  SLASSERT(result);

  /**
   * 音频缓冲区大小计算
   *
   * 业务逻辑说明：
   * - 低延迟要求越高，缓冲区应该越小
   * - 但缓冲区太小可能导致音频断续，需要平衡延迟和稳定性
   * - 在播放器开始播放前，应该尽量减少缓冲
   *
   * 计算公式：帧数 × 通道数 × 位深度（位） = 总位数
   */
  uint32_t bufSize = engine.fastPathFramesPerBuf_ * engine.sampleChannels_ *
                     engine.bitsPerSample_;
  /**
   * 位到字节的转换：
   * - (bufSize + 7) >> 3 等价于 (bufSize + 7) / 8
   * - 加 7 是为了向上取整，确保有足够的字节空间
   * - >> 3 是右移 3 位，等价于除以 8（位到字节转换）
   */
  bufSize = (bufSize + 7) >> 3;  // 位 -> 字节转换
  engine.bufCount_ = BUF_COUNT;  // 使用预定义的缓冲区数量常量

  /**
   * 分配音频缓冲区数组
   * - allocateSampleBufs: 自定义函数，分配指定数量和大小的缓冲区
   * - 返回值是 sample_buf 指针，指向缓冲区数组
   */
  engine.bufs_ = allocateSampleBufs(engine.bufCount_, bufSize);
  /**
   * C++ 断言语法：assert(表达式)
   * - 如果表达式为 false，程序会终止并显示错误信息
   * - 只在 Debug 模式下有效，Release 模式下会被移除
   * - 用于检查关键条件，确保程序正确性
   */
  assert(engine.bufs_);

  /**
   * C++ 动态内存分配语法：new 关键字
   * - new AudioQueue(参数): 在堆上创建 AudioQueue 对象
   * - 返回指向新对象的指针
   * - 需要配对使用 delete 释放内存，避免内存泄漏
   */
  engine.freeBufQueue_ =
      new AudioQueue(engine.bufCount_);                    // 创建空闲缓冲区队列
  engine.recBufQueue_ = new AudioQueue(engine.bufCount_);  // 创建录制缓冲区队列
  assert(engine.freeBufQueue_ && engine.recBufQueue_);  // 检查对象创建是否成功

  /**
   * C++ for 循环语法：for(初始化; 条件; 递增)
   * - uint32_t i = 0: 声明并初始化循环变量
   * - i < engine.bufCount_: 循环条件
   * - i++: 每次循环后递增 i
   *
   * 业务逻辑：将所有缓冲区初始化为空闲状态
   */
  for (uint32_t i = 0; i < engine.bufCount_; i++) {
    /**
     * 队列操作：将缓冲区添加到空闲队列
     * - &engine.bufs_[i]: 获取第 i 个缓冲区的地址
     * - push(): 队列的入队操作
     */
    engine.freeBufQueue_->push(&engine.bufs_[i]);
  }

  // 保存回声效果参数
  engine.echoDelay_ = delayInMs;
  engine.echoDecay_ = decay;

  /**
   * 创建音频延迟效果器对象
   * - AudioDelay 构造函数参数：采样率、通道数、位深度、延迟时间、衰减系数
   * - 这个对象负责实现回声效果的算法
   */
  engine.delayEffect_ = new AudioDelay(
      engine.fastPathSampleRate_, engine.sampleChannels_, engine.bitsPerSample_,
      engine.echoDelay_, engine.echoDecay_);
  assert(engine.delayEffect_);  // 确保对象创建成功
}

/**
 * 配置回声效果参数
 *
 * C++ 语法说明：
 * - jboolean: JNI 布尔类型，对应 Java 的 boolean
 * - 函数返回 JNI_FALSE 表示操作失败，JNI_TRUE 表示成功
 *
 * 业务逻辑：动态调整回声延迟时间和衰减系数
 */
JNIEXPORT jboolean JNICALL
Java_com_google_sample_echo_MainActivity_configureEcho(JNIEnv* env, jclass type,
                                                       jint delayInMs,
                                                       jfloat decay) {
  // 更新引擎的回声参数
  engine.echoDelay_ = delayInMs;
  engine.echoDecay_ = decay;

  /**
   * 更新音频效果器的参数
   * - 调用对象的成员函数来动态调整效果参数
   * - 这允许在音频播放过程中实时调整回声效果
   */
  engine.delayEffect_->setDelayTime(delayInMs);
  engine.delayEffect_->setDecayWeight(decay);
  return JNI_FALSE;  // 注意：此处返回 FALSE 可能是示例代码的错误，正常应该返回
                     // TRUE
}

/**
 * 创建 OpenSL ES 缓冲队列音频播放器
 *
 * 业务逻辑：
 * 1. 配置音频采样格式
 * 2. 创建音频播放器对象
 * 3. 设置缓冲区队列
 * 4. 注册回调函数
 */
JNIEXPORT jboolean JNICALL
Java_com_google_sample_echo_MainActivity_createSLBufferQueueAudioPlayer(
    JNIEnv* env, jclass type) {
  /**
   * 音频格式配置结构体
   * - SampleFormat: 自定义结构体，封装音频采样参数
   */
  SampleFormat sampleFormat;
  memset(&sampleFormat, 0, sizeof(sampleFormat));  // 初始化为零

  /**
   * 配置音频格式参数
   * - pcmFormat_: PCM 音频格式（位深度）
   * - framesPerBuf_: 每个缓冲区的音频帧数
   * - channels_: 音频通道数
   * - sampleRate_: 采样率
   */
  sampleFormat.pcmFormat_ = (uint16_t)engine.bitsPerSample_;
  sampleFormat.framesPerBuf_ = engine.fastPathFramesPerBuf_;
  sampleFormat.channels_ = (uint16_t)engine.sampleChannels_;
  sampleFormat.sampleRate_ = engine.fastPathSampleRate_;

  /**
   * 创建音频播放器对象
   * - AudioPlayer 构造函数接收采样格式和 OpenSL ES 引擎接口
   * - 播放器负责将音频数据输出到扬声器
   */
  engine.player_ = new AudioPlayer(&sampleFormat, engine.slEngineItf_);
  assert(engine.player_);

  /**
   * C++ 空指针检查
   * - nullptr 是 C++11 引入的空指针常量，比 NULL 更安全
   * - 检查对象创建是否成功，失败则返回 JNI_FALSE
   */
  if (engine.player_ == nullptr) return JNI_FALSE;

  /**
   * 设置播放器的缓冲区队列
   * - recBufQueue_: 包含录制音频数据的队列（输入）
   * - freeBufQueue_: 空闲缓冲区队列（输出）
   * - 这样播放器可以从录制队列获取数据，播放完后将缓冲区返回到空闲队列
   */
  engine.player_->SetBufQueue(engine.recBufQueue_, engine.freeBufQueue_);

  /**
   * 注册回调函数
   * - EngineService: 回调函数指针
   * - (void *)&engine: 传递给回调函数的上下文指针
   * - 当播放器需要处理音频数据时会调用这个回调函数
   */
  engine.player_->RegisterCallback(EngineService, (void*)&engine);

  return JNI_TRUE;  // 成功返回 true
}

/**
 * 删除 OpenSL ES 缓冲队列音频播放器
 *
 * 业务逻辑：
 * 1. 检查播放器对象是否存在
 * 2. 释放播放器占用的内存
 * 3. 将指针设置为 nullptr 防止悬空指针
 */
JNIEXPORT void JNICALL
Java_com_google_sample_echo_MainActivity_deleteSLBufferQueueAudioPlayer(
    JNIEnv* env, jclass type) {
  /**
   * C++ 空指针检查和内存管理
   * - if (engine.player_): 检查指针是否为空
   * - delete: C++ 内存释放关键字，释放 new 分配的内存
   * - nullptr: C++11 空指针常量，比 NULL 更安全
   */
  if (engine.player_) {
    delete engine.player_;        // 释放播放器对象占用的内存
    engine.player_ = nullptr;     // 避免悬空指针，防止重复释放
  }
}

/**
 * 创建音频录制器
 *
 * 业务逻辑：
 * 1. 配置音频录制格式
 * 2. 创建 AudioRecorder 对象
 * 3. 设置缓冲区队列
 * 4. 注册回调函数
 * 5. 返回创建结果
 */
JNIEXPORT jboolean JNICALL
Java_com_google_sample_echo_MainActivity_createAudioRecorder(JNIEnv* env,
                                                             jclass type) {
  /**
   * 配置音频录制格式
   * - 与播放器使用相同的格式配置，确保兼容性
   */
  SampleFormat sampleFormat;
  memset(&sampleFormat, 0, sizeof(sampleFormat));  // 清零初始化
  sampleFormat.pcmFormat_ = static_cast<uint16_t>(engine.bitsPerSample_);

  /**
   * 注释行：Android PCM 表示格式设置
   * - SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT: 有符号整数格式
   * - 此行被注释掉，使用默认格式
   */
  // SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
  sampleFormat.channels_ = engine.sampleChannels_;          // 通道数
  sampleFormat.sampleRate_ = engine.fastPathSampleRate_;   // 采样率
  sampleFormat.framesPerBuf_ = engine.fastPathFramesPerBuf_; // 每缓冲区帧数
  
  /**
   * 创建音频录制器对象
   * - AudioRecorder 构造函数接收采样格式和 OpenSL ES 引擎接口
   * - 录制器负责从麦克风捕获音频数据
   */
  engine.recorder_ = new AudioRecorder(&sampleFormat, engine.slEngineItf_);
  
  /**
   * 检查录制器创建是否成功
   * - 使用 ! 运算符检查指针是否为空
   * - 失败时返回 JNI_FALSE
   */
  if (!engine.recorder_) {
    return JNI_FALSE;
  }
  
  /**
   * 设置录制器的缓冲区队列
   * - freeBufQueue_: 空闲缓冲区队列（输入）
   * - recBufQueue_: 录制缓冲区队列（输出）
   * - 录制器从空闲队列获取缓冲区，录制完成后将数据放入录制队列
   */
  engine.recorder_->SetBufQueues(engine.freeBufQueue_, engine.recBufQueue_);
  
  /**
   * 注册回调函数
   * - 当录制器有新的音频数据可用时调用回调函数
   * - EngineService 函数负责处理音频数据和应用效果
   */
  engine.recorder_->RegisterCallback(EngineService, (void*)&engine);
  
  return JNI_TRUE;  // 成功返回 true
}

/**
 * 删除音频录制器
 *
 * 业务逻辑：
 * 1. 检查录制器对象是否存在
 * 2. 释放录制器占用的内存
 * 3. 将指针设置为 nullptr 防止悬空指针
 */
JNIEXPORT void JNICALL
Java_com_google_sample_echo_MainActivity_deleteAudioRecorder(JNIEnv* env,
                                                             jclass type) {
  /**
   * 删除录制器对象
   * - 使用 delete 释放动态分配的内存
   * - 即使指针为空，delete 也是安全的（不会崩溃）
   */
  if (engine.recorder_) delete engine.recorder_;

  /**
   * 设置指针为空
   * - 避免悬空指针，防止意外访问已释放的内存
   * - nullptr 是 C++11 引入的类型安全的空指针常量
   */
  engine.recorder_ = nullptr;
}

/**
 * 开始音频播放和录制
 *
 * 业务逻辑：
 * 1. 重置帧计数器
 * 2. 启动音频播放器
 * 3. 启动音频录制器
 * 4. 开始音频数据的循环处理
 */
JNIEXPORT void JNICALL
Java_com_google_sample_echo_MainActivity_startPlay(JNIEnv* env, jclass type) {
  /**
   * 重置帧计数器
   * - frameCount_ 用于统计处理的音频帧数量
   * - 每次开始播放时都重置为 0
   */
  engine.frameCount_ = 0;
  
  /**
   * 启动音频播放器
   * - 将播放器设置为等待数据状态 (waitForData state)
   * - Start() 方法返回 SL_BOOLEAN_TRUE 表示成功，SL_BOOLEAN_FALSE 表示失败
   */
  if (SL_BOOLEAN_FALSE == engine.player_->Start()) {
    /**
     * 错误处理和日志记录
     * - LOGE: 错误级别的日志宏
     * - __FUNCTION__: 编译器预定义宏，表示当前函数名
     * - ====: 日志前缀，便于在日志中快速定位错误
     */
    LOGE("====%s failed", __FUNCTION__);
    return;  // 播放器启动失败，直接返回
  }
  
  /**
   * 启动音频录制器
   * - 录制器开始从麦克风捕获音频数据
   * - 录制的数据会通过回调函数进行处理和播放
   */
  engine.recorder_->Start();
}

/**
 * 停止音频播放和录制
 *
 * 业务逻辑：
 * 1. 停止音频录制器
 * 2. 停止音频播放器
 * 3. 清理录制器和播放器对象
 * 4. 重置对象指针
 */
JNIEXPORT void JNICALL
Java_com_google_sample_echo_MainActivity_stopPlay(JNIEnv* env, jclass type) {
  /**
   * 停止音频录制器
   * - 停止从麦克风捕获音频数据
   * - 必须先停止录制器，再停止播放器，避免数据竞争
   */
  engine.recorder_->Stop();
  
  /**
   * 停止音频播放器
   * - 停止向扬声器输出音频数据
   * - 释放播放器占用的音频设备资源
   */
  engine.player_->Stop();

  /**
   * 清理对象内存
   * - 释放录制器和播放器对象占用的内存
   * - 这里直接使用 delete，没有检查指针是否为空（假设对象已正确创建）
   */
  delete engine.recorder_;
  delete engine.player_;
  
  /**
   * 重置对象指针
   * - 使用 NULL 而不是 nullptr（兼容 C 风格代码）
   * - 避免悬空指针，防止意外访问已释放的内存
   */
  engine.recorder_ = NULL;
  engine.player_ = NULL;
}

/**
 * 删除 OpenSL ES 音频引擎和相关资源
 *
 * 业务逻辑：
 * 1. 清理音频队列对象
 * 2. 释放音频缓冲区内存
 * 3. 销毁 OpenSL ES 引擎对象
 * 4. 清理延迟效果器对象
 */
JNIEXPORT void JNICALL Java_com_google_sample_echo_MainActivity_deleteSLEngine(
    JNIEnv* env, jclass type) {
  /**
   * 删除音频队列对象
   * - 释放录制缓冲区队列和空闲缓冲区队列
   * - 这些队列管理着音频数据的缓冲区分配
   */
  delete engine.recBufQueue_;   // 删除录制缓冲区队列
  delete engine.freeBufQueue_;  // 删除空闲缓冲区队列
  
  /**
   * 释放音频缓冲区内存
   * - releaseSampleBufs: 自定义函数，释放之前分配的音频缓冲区数组
   * - 第一个参数：缓冲区数组指针
   * - 第二个参数：缓冲区数量
   */
  releaseSampleBufs(engine.bufs_, engine.bufCount_);
  
  /**
   * 销毁 OpenSL ES 引擎对象
   * - 检查引擎对象是否存在（避免重复销毁）
   * - 调用 Destroy 方法释放 OpenSL ES 引擎资源
   */
  if (engine.slEngineObj_ != NULL) {
    /**
     * OpenSL ES 对象销毁语法
     * - (*engine.slEngineObj_): 解引用对象接口指针
     * - ->Destroy(): 调用对象的销毁方法
     * - 传递对象自身作为参数
     */
    (*engine.slEngineObj_)->Destroy(engine.slEngineObj_);
    engine.slEngineObj_ = NULL;  // 重置对象指针
    engine.slEngineItf_ = NULL;  // 重置接口指针
  }

  /**
   * 清理延迟效果器对象
   * - 释放回声效果处理器占用的内存
   * - 使用 nullptr 而不是 NULL（现代 C++ 风格）
   */
  if (engine.delayEffect_) {
    delete engine.delayEffect_;
    engine.delayEffect_ = nullptr;
  }
}

/**
 * 调试功能：获取音频缓冲区计数
 *
 * C++ 语法说明：
 * - uint32_t: 32 位无符号整数类型
 * - void: 函数不接收参数
 * - 此函数用于调试和监控缓冲区分配情况
 *
 * 业务逻辑：
 * 1. 统计各个组件的缓冲区使用情况
 * 2. 输出详细的缓冲区分布信息
 * 3. 检查缓冲区是否有丢失
 */
uint32_t dbgEngineGetBufCount(void) {
  /**
   * 统计播放器设备缓冲区数量
   * - dbgGetDevBufCount(): 调试方法，获取设备层缓冲区数量
   */
  uint32_t count = engine.player_->dbgGetDevBufCount();
  
  /**
   * 累加录制器设备缓冲区数量
   * - += 运算符：加法赋值，相当于 count = count + ...
   */
  count += engine.recorder_->dbgGetDevBufCount();
  
  /**
   * 累加空闲队列中的缓冲区数量
   * - size(): 队列对象的方法，返回队列中元素的数量
   */
  count += engine.freeBufQueue_->size();
  
  /**
   * 累加录制队列中的缓冲区数量
   */
  count += engine.recBufQueue_->size();

  /**
   * 输出详细的缓冲区分布日志
   * - LOGE: 错误级别日志宏（实际上这里更适合用 LOGD 调试级别）
   * - 格式字符串：%d 表示整数占位符
   * - 输出各个组件的缓冲区数量分布
   */
  LOGE(
      "Buf Disrtibutions: PlayerDev=%d, RecDev=%d, FreeQ=%d, "
      "RecQ=%d",
      engine.player_->dbgGetDevBufCount(),           // 播放器设备缓冲区数量
      engine.recorder_->dbgGetDevBufCount(),         // 录制器设备缓冲区数量
      engine.freeBufQueue_->size(),                  // 空闲队列缓冲区数量
      engine.recBufQueue_->size());                  // 录制队列缓冲区数量
  
  /**
   * 检查缓冲区完整性
   * - 比较实际统计的缓冲区总数与预期总数
   * - 如果不匹配，说明有缓冲区丢失，输出错误日志
   */
  if (count != engine.bufCount_) {
    LOGE("====Lost Bufs among the queue(supposed = %d, found = %d)", BUF_COUNT,
         count);
  }
  
  return count;  // 返回统计的缓冲区总数
}

/**
 * 引擎服务回调函数
 * 
 * 这是一个简单的消息传递机制，用于播放器和录制器与引擎之间的通信
 *
 * C++ 语法说明：
 * - bool: 布尔返回类型（true/false）
 * - void* ctx: 通用指针类型，用于传递上下文信息
 * - uint32_t msg: 32 位无符号整数，表示消息类型
 * - void* data: 通用指针类型，用于传递消息数据
 * 
 * 业务逻辑：
 * 1. 处理缓冲区统计请求
 * 2. 处理录制音频数据可用通知
 * 3. 应用音频延迟效果
 */
bool EngineService(void* ctx, uint32_t msg, void* data) {
  /**
   * 断言检查：确保上下文指针正确
   * - assert(条件): 如果条件为 false，程序会终止
   * - 这里检查传入的上下文是否是引擎对象的地址
   */
  assert(ctx == &engine);
  
  /**
   * C++ switch 语句：根据消息类型执行不同的处理逻辑
   * - switch(表达式): 根据表达式的值选择执行的分支
   * - case 常量: 匹配的分支标签
   * - break: 跳出 switch 语句
   */
  switch (msg) {
    /**
     * 处理缓冲区统计请求
     * - 当播放器或录制器需要获取缓冲区统计信息时触发
     */
    case ENGINE_SERVICE_MSG_RETRIEVE_DUMP_BUFS: {
      /**
       * C++ 类型转换和指针操作
       * - static_cast<uint32_t*>(data): 将 void* 转换为 uint32_t* 类型
       * - *(指针): 解引用操作符，获取指针指向的值
       * - 这里将缓冲区统计结果写入到 data 指向的内存地址
       */
      *(static_cast<uint32_t*>(data)) = dbgEngineGetBufCount();
      break;
    }
    
    /**
     * 处理录制音频数据可用通知
     * - 当录制器有新的音频数据可用时触发
     * - 在这里应用音频延迟效果（回声效果）
     */
    case ENGINE_SERVICE_MSG_RECORDED_AUDIO_AVAILABLE: {
      /**
       * 获取音频缓冲区数据
       * - static_cast<sample_buf*>(data): 将 void* 转换为 sample_buf* 类型
       * - sample_buf: 自定义的音频缓冲区结构体
       */
      sample_buf* buf = static_cast<sample_buf*>(data);
      
      /**
       * 验证缓冲区大小正确性
       * - 计算预期的缓冲区大小：帧数 × 通道数 × 每样本字节数
       * - buf->size_: 缓冲区实际大小
       * - engine.bitsPerSample_ / 8: 将位数转换为字节数
       */
      assert(engine.fastPathFramesPerBuf_ ==
             buf->size_ / engine.sampleChannels_ / (engine.bitsPerSample_ / 8));
      
      /**
       * 应用音频延迟效果
       * - reinterpret_cast<int16_t*>(buf->buf_): 将缓冲区数据转换为 16 位有符号整数数组
       * - process(): 延迟效果器的处理方法
       * - 第一个参数：音频数据指针
       * - 第二个参数：音频帧数
       */
      engine.delayEffect_->process(reinterpret_cast<int16_t*>(buf->buf_),
                                   engine.fastPathFramesPerBuf_);
      break;
    }
    
    /**
     * 默认分支：处理未知消息类型
     * - 这是一个编程错误，不应该收到未知的消息类型
     */
    default:
      assert(false);  // 触发断言失败，程序终止
      return false;   // 返回失败状态
  }

  return true;  // 消息处理成功
}
