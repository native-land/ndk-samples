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
 * GLES3JNI 示例程序 - C++端实现
 * 
 * 这个文件展示了如何通过JNI(Java Native Interface)将Java层的OpenGL渲染请求
 * 传递到C++层进行实际的OpenGL ES渲染操作。
 * 
 * 架构说明：
 * 1. Java层：GLES3JNIView继承GLSurfaceView，设置OpenGL上下文和渲染器
 * 2. Java层：GLES3JNILib提供native方法声明，加载C++动态库
 * 3. C++层：本文件实现JNI接口函数，管理OpenGL渲染器
 * 
 * JNI关联机制：
 * - Java中的native方法通过特定的命名规则映射到C++函数
 * - 例如：Java_com_android_gles3jni_GLES3JNILib_init 对应 GLES3JNILib.init()
 * - GLSurfaceView的渲染回调最终通过JNI调用到C++的渲染函数
 */

#include "gles3jni.h"

#include <jni.h>        // JNI接口头文件
#include <stdlib.h>     // 标准库函数
#include <string.h>     // 字符串操作函数
#include <time.h>       // 时间相关函数

/**
 * 四边形顶点数据定义
 * 定义一个正方形的四个顶点，包含位置坐标和颜色信息
 * 对角线长度 < 2，确保在任何旋转角度下都能完全显示在 [-1..1]^2 的标准化坐标系中
 */
const Vertex QUAD[4] = {
    // 左下角顶点：位置(-0.7, -0.7)，颜色为绿色(0, 255, 0)
    {{-0.7f, -0.7f}, {0x00, 0xFF, 0x00}},
    // 右下角顶点：位置(0.7, -0.7)，颜色为蓝色(0, 0, 255)
    {{0.7f, -0.7f}, {0x00, 0x00, 0xFF}},
    // 左上角顶点：位置(-0.7, 0.7)，颜色为红色(255, 0, 0)
    {{-0.7f, 0.7f}, {0xFF, 0x00, 0x00}},
    // 右上角顶点：位置(0.7, 0.7)，颜色为白色(255, 255, 255)
    {{0.7f, 0.7f}, {0xFF, 0xFF, 0xFF}},
};

/**
 * OpenGL错误检查函数
 * 检查OpenGL操作是否产生错误，用于调试和错误处理
 * 
 * @param funcName 调用此检查的函数名称，用于错误日志输出
 * @return true表示有错误发生，false表示无错误
 */
bool checkGlError(const char* funcName) {
  GLint err = glGetError();  // 获取最近的OpenGL错误代码
  if (err != GL_NO_ERROR) {
    // 如果有错误，输出错误日志，包含函数名和错误代码
    ALOGE("GL error after %s(): 0x%08x\n", funcName, err);
    return true;
  }
  return false;  // 无错误
}

/**
 * 创建并编译OpenGL着色器
 * 
 * @param shaderType 着色器类型（GL_VERTEX_SHADER顶点着色器 或 GL_FRAGMENT_SHADER片段着色器）
 * @param src 着色器源代码字符串
 * @return 编译成功返回着色器ID，失败返回0
 */
GLuint createShader(GLenum shaderType, const char* src) {
  // 创建着色器对象
  GLuint shader = glCreateShader(shaderType);
  if (!shader) {
    checkGlError("glCreateShader");
    return 0;
  }
  
  // 设置着色器源代码
  glShaderSource(shader, 1, &src, NULL);

  // 编译着色器
  GLint compiled = GL_FALSE;
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  
  // 检查编译结果
  if (!compiled) {
    // 获取编译错误信息
    GLint infoLogLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLen);
    if (infoLogLen > 0) {
      GLchar* infoLog = (GLchar*)malloc(infoLogLen);
      if (infoLog) {
        glGetShaderInfoLog(shader, infoLogLen, NULL, infoLog);
        // 输出编译错误日志
        ALOGE("Could not compile %s shader:\n%s\n",
              shaderType == GL_VERTEX_SHADER ? "vertex" : "fragment", infoLog);
        free(infoLog);
      }
    }
    // 编译失败，删除着色器对象
    glDeleteShader(shader);
    return 0;
  }

  return shader;  // 返回编译成功的着色器ID
}

/**
 * 创建OpenGL着色器程序
 * 将顶点着色器和片段着色器链接成一个完整的着色器程序
 * 
 * @param vtxSrc 顶点着色器源代码
 * @param fragSrc 片段着色器源代码
 * @return 链接成功返回程序ID，失败返回0
 */
GLuint createProgram(const char* vtxSrc, const char* fragSrc) {
  GLuint vtxShader = 0;   // 顶点着色器ID
  GLuint fragShader = 0;  // 片段着色器ID
  GLuint program = 0;     // 着色器程序ID
  GLint linked = GL_FALSE; // 链接状态

  // 创建并编译顶点着色器
  vtxShader = createShader(GL_VERTEX_SHADER, vtxSrc);
  if (!vtxShader) goto exit;

  // 创建并编译片段着色器
  fragShader = createShader(GL_FRAGMENT_SHADER, fragSrc);
  if (!fragShader) goto exit;

  // 创建着色器程序对象
  program = glCreateProgram();
  if (!program) {
    checkGlError("glCreateProgram");
    goto exit;
  }
  
  // 将着色器附加到程序
  glAttachShader(program, vtxShader);
  glAttachShader(program, fragShader);

  // 链接着色器程序
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  
  // 检查链接结果
  if (!linked) {
    ALOGE("Could not link program");
    // 获取链接错误信息
    GLint infoLogLen = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLen);
    if (infoLogLen) {
      GLchar* infoLog = (GLchar*)malloc(infoLogLen);
      if (infoLog) {
        glGetProgramInfoLog(program, infoLogLen, NULL, infoLog);
        ALOGE("Could not link program:\n%s\n", infoLog);
        free(infoLog);
      }
    }
    // 链接失败，删除程序对象
    glDeleteProgram(program);
    program = 0;
  }

exit:
  // 清理着色器对象（程序链接后不再需要）
  glDeleteShader(vtxShader);
  glDeleteShader(fragShader);
  return program;
}

/**
 * 打印OpenGL信息的辅助函数
 * 用于调试，输出OpenGL的版本、厂商等信息
 * 
 * @param name 信息类型名称
 * @param s OpenGL信息枚举值（如GL_VERSION, GL_VENDOR等）
 */
static void printGlString(const char* name, GLenum s) {
  const char* v = (const char*)glGetString(s);
  ALOGV("GL %s: %s\n", name, v);
}

// ----------------------------------------------------------------------------
// Renderer类实现 - OpenGL渲染器基类
// 管理多个旋转的四边形实例的渲染
// ----------------------------------------------------------------------------

/**
 * Renderer构造函数
 * 初始化渲染器的所有成员变量
 */
Renderer::Renderer() : mNumInstances(0), mLastFrameNs(0) {
  memset(mScale, 0, sizeof(mScale));           // 清零缩放数组
  memset(mAngularVelocity, 0, sizeof(mAngularVelocity)); // 清零角速度数组
  memset(mAngles, 0, sizeof(mAngles));         // 清零角度数组
}

/**
 * Renderer析构函数
 */
Renderer::~Renderer() {}

/**
 * 处理屏幕尺寸变化
 * 当GLSurfaceView的尺寸改变时调用，重新计算场景参数和实例位置
 * 
 * @param w 新的屏幕宽度
 * @param h 新的屏幕高度
 */
void Renderer::resize(int w, int h) {
  // 映射偏移缓冲区，用于存储每个实例的位置偏移
  auto offsets = mapOffsetBuf();
  // 根据新的屏幕尺寸计算场景参数和实例位置
  calcSceneParams(w, h, offsets);
  // 解除偏移缓冲区映射
  unmapOffsetBuf();

  // 为每个实例初始化随机的旋转角度和角速度
  // 注意：auto推导为signed int，需要强制转换为unsigned
  for (auto i = (unsigned)0; i < mNumInstances; i++) {
    mAngles[i] = drand48() * TWO_PI;  // 随机初始角度 [0, 2π]
    // 随机角速度，范围为 [-MAX_ROT_SPEED, MAX_ROT_SPEED]
    mAngularVelocity[i] = MAX_ROT_SPEED * (2.0 * drand48() - 1.0);
  }

  mLastFrameNs = 0;  // 重置上一帧时间戳

  // 设置OpenGL视口为新的屏幕尺寸
  glViewport(0, 0, w, h);
}

/**
 * 计算场景参数和实例位置
 * 根据屏幕尺寸计算网格布局，确定每个四边形实例的位置和缩放
 * 
 * 坐标系统说明：
 * - 模型空间：四边形在 [-1..1]^2 空间中
 * - 场景空间：横屏 [-1..1] x [-1/(2*w/h)..1/(2*w/h)]，竖屏 [-1/(2*h/w)..1/(2*h/w)] x [-1..1]
 * - 裁剪空间：OpenGL标准的 [-1..1]^2 空间
 * 
 * @param w 屏幕宽度
 * @param h 屏幕高度
 * @param offsets 输出参数，存储每个实例的位置偏移
 */
void Renderer::calcSceneParams(unsigned int w, unsigned int h, float* offsets) {
  // 沿屏幕较长边的网格单元数量
  const float NCELLS_MAJOR = MAX_INSTANCES_PER_SIDE;
  // 场景空间中的单元格大小
  const float CELL_SIZE = 2.0f / NCELLS_MAJOR;

  // 计算以"横屏"模式进行，即假设 dim[0] >= dim[1]
  // 如果是竖屏(h > w)，最后会调整坐标顺序
  const float dim[2] = {fmaxf(w, h), fminf(w, h)};  // [长边, 短边]
  const float aspect[2] = {dim[0] / dim[1], dim[1] / dim[0]};  // [长宽比, 宽长比]
  const float scene2clip[2] = {1.0f, aspect[0]};  // 场景到裁剪空间的缩放因子
  
  // 计算每个维度的网格单元数量
  const int ncells[2] = {static_cast<int>(NCELLS_MAJOR),  // 长边方向的单元数
                         (int)floorf(NCELLS_MAJOR * aspect[1])};  // 短边方向的单元数

  // 计算每个维度上网格中心点的位置
  float centers[2][MAX_INSTANCES_PER_SIDE];
  for (int d = 0; d < 2; d++) {
    auto offset = -ncells[d] / NCELLS_MAJOR;  // 起始偏移，对于d=0是-1.0
    for (auto i = 0; i < ncells[d]; i++) {
      // 计算第i个单元格的中心位置
      centers[d][i] = scene2clip[d] * (CELL_SIZE * (i + 0.5f) + offset);
    }
  }

  // 确定主轴和次轴（根据屏幕方向）
  int major = w >= h ? 0 : 1;  // 横屏时主轴是x(0)，竖屏时主轴是y(1)
  int minor = w >= h ? 1 : 0;  // 横屏时次轴是y(1)，竖屏时次轴是x(0)
  
  // 生成所有实例的位置偏移（centers[0]和centers[1]的外积）
  for (int i = 0; i < ncells[0]; i++) {
    for (int j = 0; j < ncells[1]; j++) {
      int idx = i * ncells[1] + j;  // 实例索引
      offsets[2 * idx + major] = centers[0][i];  // 主轴坐标
      offsets[2 * idx + minor] = centers[1][j];  // 次轴坐标
    }
  }

  // 设置实例总数和缩放参数
  mNumInstances = ncells[0] * ncells[1];
  mScale[major] = 0.5f * CELL_SIZE * scene2clip[0];  // 主轴缩放
  mScale[minor] = 0.5f * CELL_SIZE * scene2clip[1];  // 次轴缩放
}

/**
 * 更新动画状态
 * 计算每个实例的旋转角度和变换矩阵，实现旋转动画效果
 */
void Renderer::step() {
  // 获取当前时间（高精度）
  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  auto nowNs = now.tv_sec * 1000000000ull + now.tv_nsec;  // 转换为纳秒

  // 如果不是第一帧，计算时间差并更新动画
  if (mLastFrameNs > 0) {
    // 计算帧间时间差（秒）
    float dt = float(nowNs - mLastFrameNs) * 0.000000001f;

    // 更新每个实例的旋转角度
    for (unsigned int i = 0; i < mNumInstances; i++) {
      mAngles[i] += mAngularVelocity[i] * dt;  // 角度 = 角速度 × 时间
      
      // 保持角度在 [-2π, 2π] 范围内，避免浮点数溢出
      if (mAngles[i] >= TWO_PI) {
        mAngles[i] -= TWO_PI;
      } else if (mAngles[i] <= -TWO_PI) {
        mAngles[i] += TWO_PI;
      }
    }

    // 映射变换缓冲区并更新变换矩阵
    float* transforms = mapTransformBuf();
    for (unsigned int i = 0; i < mNumInstances; i++) {
      float s = sinf(mAngles[i]);  // sin值
      float c = cosf(mAngles[i]);  // cos值
      
      // 构建2D旋转+缩放矩阵（以行主序存储）
      // | c*sx  -s*sx |
      // | s*sy   c*sy |
      transforms[4 * i + 0] = c * mScale[0];   // 矩阵[0,0]
      transforms[4 * i + 1] = s * mScale[1];   // 矩阵[1,0]
      transforms[4 * i + 2] = -s * mScale[0];  // 矩阵[0,1]
      transforms[4 * i + 3] = c * mScale[1];   // 矩阵[1,1]
    }
    unmapTransformBuf();  // 解除变换缓冲区映射
  }

  mLastFrameNs = nowNs;  // 记录当前帧时间戳
}

/**
 * 执行渲染
 * 更新动画状态并绘制所有实例
 */
void Renderer::render() {
  step();  // 更新动画状态

  // 设置清屏颜色为深蓝灰色
  glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
  // 清除颜色缓冲区和深度缓冲区
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // 绘制所有实例（具体实现在ES2/ES3子类中）
  draw(mNumInstances);
  // 检查渲染过程中是否有OpenGL错误
  checkGlError("Renderer::render");
}

// ----------------------------------------------------------------------------
// JNI接口实现 - 连接Java层和C++层的桥梁
// 这些函数通过特定的命名规则与Java中的native方法对应
// ----------------------------------------------------------------------------

// 全局渲染器指针，在JNI函数间共享
static Renderer* g_renderer = NULL;

// JNI函数声明
extern "C" {
JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_init(JNIEnv* env,
                                                                  jobject obj);
JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_resize(
    JNIEnv* env, jobject obj, jint width, jint height);

JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_step(JNIEnv* env,
                                                                  jobject obj);
};

#if !defined(DYNAMIC_ES3)
// 静态链接时的OpenGL ES 3.0初始化桩函数
static GLboolean gl3stubInit() { return GL_TRUE; }
#endif

/**
 * JNI初始化函数
 * 对应Java中的GLES3JNILib.init()方法
 * 在GLSurfaceView.Renderer.onSurfaceCreated()中被调用
 * 
 * 功能：检测OpenGL ES版本并创建相应的渲染器
 */
JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_init(JNIEnv* env,
                                                                  jobject obj) {
  // 如果已有渲染器，先清理
  if (g_renderer) {
    delete g_renderer;
    g_renderer = NULL;
  }

  // 打印OpenGL信息用于调试
  printGlString("Version", GL_VERSION);
  printGlString("Vendor", GL_VENDOR);
  printGlString("Renderer", GL_RENDERER);
  printGlString("Extensions", GL_EXTENSIONS);

  // 根据OpenGL ES版本创建相应的渲染器
  const char* versionStr = (const char*)glGetString(GL_VERSION);
  if (strstr(versionStr, "OpenGL ES 3.") && gl3stubInit()) {
    // 支持OpenGL ES 3.x，创建ES3渲染器
    g_renderer = createES3Renderer();
  } else if (strstr(versionStr, "OpenGL ES 2.")) {
    // 仅支持OpenGL ES 2.x，创建ES2渲染器
    g_renderer = createES2Renderer();
  } else {
    // 不支持的OpenGL ES版本
    ALOGE("Unsupported OpenGL ES version");
  }
}

/**
 * JNI尺寸变化处理函数
 * 对应Java中的GLES3JNILib.resize(int, int)方法
 * 在GLSurfaceView.Renderer.onSurfaceChanged()中被调用
 * 
 * @param width 新的屏幕宽度
 * @param height 新的屏幕高度
 */
JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_resize(
    JNIEnv* env, jobject obj, jint width, jint height) {
  if (g_renderer) {
    g_renderer->resize(width, height);
  }
}

/**
 * JNI渲染函数
 * 对应Java中的GLES3JNILib.step()方法
 * 在GLSurfaceView.Renderer.onDrawFrame()中被调用
 * 
 * 这是连接Java层GLSurfaceView和C++层OpenGL渲染的关键函数
 * 每一帧都会调用此函数来更新和绘制场景
 */
JNIEXPORT void JNICALL Java_com_android_gles3jni_GLES3JNILib_step(JNIEnv* env,
                                                                  jobject obj) {
  if (g_renderer) {
    g_renderer->render();  // 执行实际的渲染操作
  }
}
