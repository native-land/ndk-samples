/*
 * Copyright 2013 The Android Open Source Project
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
 * GLES3JNI 头文件
 * 
 * 这个头文件定义了OpenGL ES 2.0/3.0渲染示例的核心接口和数据结构。
 * 主要功能：
 * 1. 提供跨平台的OpenGL ES版本兼容性
 * 2. 定义渲染器基类和相关数据结构
 * 3. 声明OpenGL辅助函数
 * 4. 配置日志和调试宏
 */

#ifndef GLES3JNI_H
#define GLES3JNI_H 1

#include <android/log.h>  // Android日志系统
#include <math.h>         // 数学函数库

// OpenGL ES版本兼容性处理
#if DYNAMIC_ES3
#include "gl3stub.h"  // 动态加载OpenGL ES 3.0的桩文件
#else
// 根据Android API级别包含相应的OpenGL ES头文件
// 静态链接时选择最新可用的OpenGL版本
#if __ANDROID_API__ >= 24
#include <GLES3/gl32.h>  // OpenGL ES 3.2 (Android API 24+)
#elif __ANDROID_API__ >= 21
#include <GLES3/gl31.h>  // OpenGL ES 3.1 (Android API 21+)
#else
#include <GLES3/gl3.h>   // OpenGL ES 3.0 (Android API 18+)
#endif

#endif

// 调试和日志配置
#define DEBUG 1  // 启用调试模式

// Android日志系统宏定义
#define LOG_TAG "GLES3JNI"  // 日志标签，用于过滤日志输出
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)    // 错误日志宏
#if DEBUG
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)  // 详细日志宏（仅在调试模式下启用）
#else
#define ALOGV(...)  // 发布模式下禁用详细日志
#endif

// ----------------------------------------------------------------------------
// ES2和ES3渲染器共用的类型、函数和数据定义
// 具体实现在gles3jni.cpp中

// 渲染参数常量定义
#define MAX_INSTANCES_PER_SIDE 16                                          // 每边最大实例数
#define MAX_INSTANCES (MAX_INSTANCES_PER_SIDE * MAX_INSTANCES_PER_SIDE)    // 最大实例总数（16x16=256）
#define TWO_PI (2.0 * M_PI)                                               // 2π常量
#define MAX_ROT_SPEED (0.3 * TWO_PI)                                      // 最大旋转速度

/**
 * 坐标系统说明：
 * 本演示使用三个坐标空间：
 * 
 * 1. 模型空间：四边形模型位于[-1 .. 1]²空间中
 * 
 * 2. 场景空间：根据屏幕方向不同
 *    - 横屏模式：[-1 .. 1] x [-1/(2*w/h) .. 1/(2*w/h)]
 *    - 竖屏模式：[-1/(2*h/w) .. 1/(2*h/w)] x [-1 .. 1]
 * 
 * 3. 裁剪空间：OpenGL标准的[-1 .. 1]²空间
 * 
 * 变换流程：
 * 概念上，四边形首先在模型空间中旋转，然后进行统一缩放和平移
 * 以放置到场景空间中。场景空间再进行非统一缩放到裁剪空间。
 * 实际实现中，这些变换被合并，顶点直接从模型空间变换到裁剪空间。
 */

/**
 * 顶点数据结构
 * 包含位置坐标和RGBA颜色信息
 */
struct Vertex {
  GLfloat pos[2];    // 2D位置坐标 (x, y)
  GLubyte rgba[4];   // RGBA颜色值 (r, g, b, a)
};

/**
 * 四边形顶点数据数组
 * 定义了一个标准的四边形模型
 */
extern const Vertex QUAD[4];

// OpenGL ES 辅助函数声明

/**
 * 创建着色器对象
 * @param shaderType 着色器类型（GL_VERTEX_SHADER 或 GL_FRAGMENT_SHADER）
 * @param src 着色器源代码字符串
 * @return 着色器对象ID，失败时返回0
 */
extern GLuint createShader(GLenum shaderType, const char* src);

/**
 * 创建着色器程序
 * @param vtxSrc 顶点着色器源代码
 * @param fragSrc 片段着色器源代码
 * @return 着色器程序ID，失败时返回0
 */
extern GLuint createProgram(const char* vtxSrc, const char* fragSrc);

/**
 * 检查OpenGL错误
 * @param funcName 函数名称，用于错误日志输出
 * @return 如果发生GL错误返回true，否则返回false
 */
extern bool checkGlError(const char* funcName);

// ----------------------------------------------------------------------------
// OpenGL ES 2.0/3.0 渲染器接口，供JNI代码使用

/**
 * 渲染器抽象基类
 * 
 * 这个类定义了OpenGL ES渲染的通用接口，支持ES2和ES3两种实现。
 * 主要功能：
 * 1. 管理渲染场景的几何参数和动画状态
 * 2. 提供统一的渲染接口
 * 3. 抽象化缓冲区管理，由子类实现具体的ES版本特性
 */
class Renderer {
public:
    /**
     * 虚析构函数，确保子类正确析构
     */
    virtual ~Renderer();
    
    /**
     * 调整渲染视口大小
     * @param w 视口宽度
     * @param h 视口高度
     */
    void resize(int w, int h);
    
    /**
     * 执行一帧渲染
     */
    void render();

protected:
    /**
     * 受保护的构造函数，只能由子类调用
     */
    Renderer();

    // 抽象缓冲区管理接口，由子类实现具体的ES版本特性
    
    /**
     * 映射偏移缓冲区（用于实例位置数据）
     * @return 指向MAX_INSTANCES * sizeof(vec2)大小缓冲区的指针
     */
    virtual float* mapOffsetBuf() = 0;
    
    /**
     * 解除偏移缓冲区映射
     */
    virtual void unmapOffsetBuf() = 0;
    
    /**
     * 映射变换缓冲区（用于实例变换矩阵数据）
     * @return 指向MAX_INSTANCES * sizeof(mat2)大小缓冲区的指针
     */
    virtual float* mapTransformBuf() = 0;
    
    /**
     * 解除变换缓冲区映射
     */
    virtual void unmapTransformBuf() = 0;
    
    /**
     * 绘制指定数量的实例
     * @param numInstances 要绘制的实例数量
     */
    virtual void draw(unsigned int numInstances) = 0;

private:
    /**
     * 计算场景参数（实例位置、数量等）
     * @param w 视口宽度
     * @param h 视口高度
     * @param offsets 输出的偏移数组
     */
    void calcSceneParams(unsigned int w, unsigned int h, float* offsets);
    
    /**
     * 更新动画状态（旋转角度等）
     */
    void step();

    // 场景和动画状态变量
    unsigned int mNumInstances;              // 实例数量
    float mScale[2];                         // 缩放因子数组 [主轴, 次轴]
    float mAngularVelocity[MAX_INSTANCES];   // 每个实例的角速度数组
    uint64_t mLastFrameNs;                   // 上一帧时间戳（纳秒）
    float mAngles[MAX_INSTANCES];            // 每个实例的旋转角度数组

    // 禁用拷贝构造和赋值操作
    Renderer(const Renderer& rhs);
    Renderer& operator=(const Renderer& rhs);
};

// 渲染器工厂函数

/**
 * 创建OpenGL ES 2.0渲染器实例
 * @return ES2渲染器指针，失败时返回nullptr
 */
extern Renderer* createES2Renderer();

/**
 * 创建OpenGL ES 3.0渲染器实例
 * @return ES3渲染器指针，失败时返回nullptr
 */
extern Renderer* createES3Renderer();

#endif // GLES3JNI_H
