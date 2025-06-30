/*
 * Copyright (C) 2010 The Android Open Source Project
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
 *
 */

// BEGIN_INCLUDE(all)
// 头文件包含部分 - C++语法：#include预处理指令用于包含其他文件
#include <EGL/egl.h>                    // EGL图形库接口，用于创建OpenGL ES上下文
#include <GLES/gl.h>                    // OpenGL ES图形渲染库
#include <android/choreographer.h>      // Android编舞者API，用于同步渲染与显示刷新
#include <android/log.h>                // Android日志系统
#include <android/sensor.h>             // Android传感器API
#include <android/set_abort_message.h>  // 设置程序崩溃消息
#include <android_native_app_glue.h>    // Android原生应用胶水代码
#include <jni.h>                        // Java本地接口

// C++标准库头文件
#include <cassert>                      // 断言宏
#include <cerrno>                       // 错误码定义
#include <cstdlib>                      // 标准库函数（malloc, free等）
#include <cstring>                      // 字符串操作函数
#include <initializer_list>             // C++11初始化列表支持
#include <memory>                       // 智能指针等内存管理工具

// C++语法：#define宏定义，用于定义常量和函数式宏
#define LOG_TAG "native-activity"  // 定义日志标签，用于标识日志来源

// C++语法：可变参数宏定义，使用...表示可变参数，__VA_ARGS__展开参数
// __VA_OPT__是C++20特性，当可变参数为空时不展开逗号
#define _LOG(priority, fmt, ...) \
  ((void)__android_log_print((priority), (LOG_TAG), (fmt)__VA_OPT__(, ) __VA_ARGS__))

// 定义不同级别的日志宏，方便调用
#define LOGE(fmt, ...) _LOG(ANDROID_LOG_ERROR, (fmt)__VA_OPT__(, ) __VA_ARGS__)  // 错误日志
#define LOGW(fmt, ...) _LOG(ANDROID_LOG_WARN, (fmt)__VA_OPT__(, ) __VA_ARGS__)   // 警告日志
#define LOGI(fmt, ...) _LOG(ANDROID_LOG_INFO, (fmt)__VA_OPT__(, ) __VA_ARGS__)   // 信息日志

// C++语法：[[noreturn]]是C++11属性，表示函数不会返回
// __attribute__是GCC编译器属性，__format__指定printf格式检查
// static关键字表示函数只在当前文件内可见
[[noreturn]] __attribute__((__format__(__printf__, 1, 2))) static void fatal(
    const char* fmt, ...) {  // 可变参数函数，用于格式化错误消息
  va_list ap;                // C语法：可变参数列表类型
  va_start(ap, fmt);         // 初始化可变参数列表
  char* buf;                 // 用于存储格式化后的字符串
  
  // vasprintf动态分配内存并格式化字符串
  if (vasprintf(&buf, fmt, ap) < 0) {
    android_set_abort_message("failed for format error message");
  } else {
    android_set_abort_message(buf);  // 设置程序崩溃时显示的消息
    // 同时输出到日志，因为Android Studio默认过滤器会隐藏堆栈跟踪
    LOGE("%s", buf);
  }
  std::abort();  // C++标准库函数，终止程序执行
}

// C++语法：do-while(false)宏技巧，确保宏在任何上下文中都能正确工作
// #value是预处理器字符串化操作符，将参数转换为字符串
// __PRETTY_FUNCTION__和__LINE__是编译器预定义宏，提供函数名和行号
#define CHECK_NOT_NULL(value)                                           \
  do {                                                                  \
    if ((value) == nullptr) {                                           \
      fatal("%s:%d:%s must not be null", __PRETTY_FUNCTION__, __LINE__, \
            #value);                                                    \
    }                                                                   \
  } while (false)  // 空指针检查宏，如果值为空则终止程序

/**
 * 保存的状态数据结构体
 * C++语法：struct定义结构体，成员默认为public访问权限
 */
struct SavedState {
  float angle;    // 旋转角度，用于动画效果
  int32_t x;      // 触摸点的X坐标
  int32_t y;      // 触摸点的Y坐标
};

/**
 * 应用程序的共享状态引擎类
 * C++语法：struct在C++中等同于class，但成员默认为public
 * 业务逻辑：管理应用的渲染、传感器、输入等核心功能
 */
struct Engine {
  // Android应用程序状态指针
  android_app* app;

  // 传感器相关成员变量
  ASensorManager* sensorManager;        // 传感器管理器
  const ASensor* accelerometerSensor;   // 加速度计传感器（const表示不可修改）
  ASensorEventQueue* sensorEventQueue;  // 传感器事件队列

  // EGL/OpenGL相关成员变量
  EGLDisplay display;   // EGL显示设备
  EGLSurface surface;   // EGL绘制表面
  EGLContext context;   // EGL渲染上下文
  int32_t width;        // 屏幕宽度
  int32_t height;       // 屏幕高度
  SavedState state;     // 保存的状态数据

  /**
   * 创建传感器监听器
   * C++语法：void表示无返回值，参数是函数指针类型
   * 业务逻辑：初始化加速度计传感器并设置回调函数
   */
  void CreateSensorListener(ALooper_callbackFunc callback) {
    CHECK_NOT_NULL(app);  // 检查app指针是否为空

    // 获取传感器管理器实例（单例模式）
    sensorManager = ASensorManager_getInstance();
    if (sensorManager == nullptr) {
      return;  // 如果获取失败则直接返回
    }

    // 获取默认的加速度计传感器
    accelerometerSensor = ASensorManager_getDefaultSensor(
        sensorManager, ASENSOR_TYPE_ACCELEROMETER);
    
    // 创建传感器事件队列，绑定到应用的事件循环
    sensorEventQueue = ASensorManager_createEventQueue(
        sensorManager, app->looper, ALOOPER_POLL_CALLBACK, callback, this);
  }

  /**
   * 恢复应用程序的运行
   * 业务逻辑：启动渲染循环，开始接收Choreographer回调
   */
  void Resume() {
    // 检查确保不会重复调度Choreographer
    if (!running_) {
      running_ = true;     // 设置运行状态为true
      ScheduleNextTick();  // 调度下一帧
    }
  }

  /**
   * 暂停应用程序的运行
   * 业务逻辑：暂停时传感器和输入事件仍会处理，但更新和渲染循环会停止
   */
  void Pause() { 
    running_ = false;  // 设置运行状态为false
  }

 private:  // C++语法：private访问修饰符，只有类内部可以访问
  bool running_;  // 应用运行状态标志

  /**
   * 调度下一帧渲染
   * 业务逻辑：向Choreographer注册回调，在下一个垂直同步信号时执行
   */
  void ScheduleNextTick() {
    AChoreographer_postFrameCallback(AChoreographer_getInstance(), Tick, this);
  }

  /**
   * Choreographer的入口点（静态回调函数）
   * C++语法：static成员函数不依赖于类实例，可以通过类名直接调用
   * 
   * 注意：第一个参数（帧时间）在此示例中未使用。如果要使用该参数，
   * 需要注意API bug：在32位系统上，时间是有符号32位纳秒计数器，
   * 大约每2秒会溢出。如果minSdkVersion >= 29，建议使用
   * AChoreographer_postFrameCallback64（所有架构都是64位）。
   * 
   * @param data 被调度的Engine实例指针
   */
  static void Tick(long, void* data) {
    CHECK_NOT_NULL(data);
    // C++语法：reinterpret_cast强制类型转换，将void*转换为Engine*
    auto engine = reinterpret_cast<Engine*>(data);
    engine->DoTick();  // 调用实例方法
  }

  /**
   * 执行一帧的更新和渲染
   * 业务逻辑：检查运行状态，调度下一帧，更新状态，绘制帧
   */
  void DoTick() {
    if (!running_) {
      return;  // 如果未运行则直接返回
    }

    // 输入和传感器反馈通过各自的回调处理
    // Choreographer确保这些回调在此回调之前运行

    // Choreographer不会持续调度回调，我们必须在每次被调用时重新注册
    ScheduleNextTick();
    Update();     // 更新应用状态
    DrawFrame();  // 绘制帧
  }

  /**
   * 更新应用状态
   * 业务逻辑：更新动画角度，实现简单的循环动画效果
   */
  void Update() {
    state.angle += .01f;  // 每帧增加角度
    if (state.angle > 1) {
      state.angle = 0;    // 角度超过1时重置为0，形成循环
    }
  }

  /**
   * 绘制一帧
   * 业务逻辑：使用OpenGL ES清除屏幕并填充颜色，颜色基于触摸位置和动画角度
   */
  void DrawFrame() {
    if (display == nullptr) {
      return;  // 没有显示设备则直接返回
    }

    // 根据触摸位置和动画角度设置清除颜色
    // C++语法：强制类型转换float，将整数坐标转换为0-1范围的颜色值
    glClearColor(((float)state.x) / width, state.angle,
                 ((float)state.y) / height, 1);
    glClear(GL_COLOR_BUFFER_BIT);  // 清除颜色缓冲区

    eglSwapBuffers(display, surface);  // 交换前后缓冲区，显示渲染结果
  }
};

/**
 * 为当前显示设备初始化EGL上下文
 * C++语法：static函数只在当前文件内可见，返回int表示成功(0)或失败(-1)
 * 业务逻辑：设置OpenGL ES渲染环境，创建显示表面和渲染上下文
 */
static int engine_init_display(Engine* engine) {
  // 初始化OpenGL ES和EGL

  /*
   * 指定所需配置的属性
   * C++语法：const数组，EGL_NONE作为数组结束标记
   * 选择至少每个颜色分量8位且兼容屏幕窗口的EGLConfig
   */
  const EGLint attribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,  // 窗口类型表面
                            EGL_BLUE_SIZE,    8,              // 蓝色分量8位
                            EGL_GREEN_SIZE,   8,              // 绿色分量8位
                            EGL_RED_SIZE,     8,              // 红色分量8位
                            EGL_NONE};                        // 属性列表结束标记
  
  // 局部变量声明
  EGLint w, h, format;           // 宽度、高度、格式
  EGLint numConfigs;             // 可用配置数量
  EGLConfig config = nullptr;    // C++语法：nullptr是C++11空指针常量
  EGLSurface surface;            // EGL绘制表面
  EGLContext context;            // EGL渲染上下文

  // 获取默认EGL显示设备
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

  // 初始化EGL显示设备，nullptr表示不需要版本信息
  eglInitialize(display, nullptr, nullptr);

  /* 应用程序选择所需的配置
   * 尽可能找到最佳匹配，否则使用第一个可用配置
   */
  
  // 第一次调用：获取可用配置的数量
  eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);
  
  // C++语法：std::unique_ptr智能指针，自动管理数组内存
  // new EGLConfig[numConfigs]动态分配数组
  std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
  assert(supportedConfigs);  // 断言确保内存分配成功
  
  // 第二次调用：获取实际的配置列表
  eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs,
                  &numConfigs);
  assert(numConfigs);  // 断言确保至少有一个配置可用
  
  // C++语法：auto关键字自动推导类型为int
  auto i = 0;
  
  // 遍历所有配置，寻找精确匹配的配置（RGB各8位，深度缓冲0位）
  for (; i < numConfigs; i++) {
    auto& cfg = supportedConfigs[i];  // C++语法：引用，避免复制
    EGLint r, g, b, d;  // 红、绿、蓝、深度位数
    
    // 获取配置属性并检查是否符合要求
    if (eglGetConfigAttrib(display, cfg, EGL_RED_SIZE, &r) &&
        eglGetConfigAttrib(display, cfg, EGL_GREEN_SIZE, &g) &&
        eglGetConfigAttrib(display, cfg, EGL_BLUE_SIZE, &b) &&
        eglGetConfigAttrib(display, cfg, EGL_DEPTH_SIZE, &d) && 
        r == 8 && g == 8 && b == 8 && d == 0) {  // 精确匹配条件
      config = supportedConfigs[i];
      break;  // 找到匹配配置，跳出循环
    }
  }
  
  // 如果没有找到精确匹配，使用第一个可用配置
  if (i == numConfigs) {
    config = supportedConfigs[0];
  }

  // 检查是否成功获取到配置
  if (config == nullptr) {
    LOGW("Unable to initialize EGLConfig");
    return -1;  // 返回错误码
  }

  /* EGL_NATIVE_VISUAL_ID是EGLConfig的一个属性，
   * 保证被ANativeWindow_setBuffersGeometry()接受。
   * 一旦选择了EGLConfig，就可以安全地重新配置
   * ANativeWindow缓冲区以匹配，使用EGL_NATIVE_VISUAL_ID。 */
  eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
  
  // 创建窗口表面，将EGL与Android原生窗口关联
  surface = eglCreateWindowSurface(display, config, engine->app->window, nullptr);

  /* 这里没有指定OpenGL版本，将默认使用OpenGL 1.0。
   * 如果要使用OpenGL的新特性（如着色器），需要修改此处。 */
  context = eglCreateContext(display, config, nullptr, nullptr);

  // 将上下文设置为当前上下文，绑定到读取和绘制表面
  if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
    LOGW("Unable to eglMakeCurrent");
    return -1;  // 设置失败，返回错误码
  }

  // 查询表面的宽度和高度
  eglQuerySurface(display, surface, EGL_WIDTH, &w);
  eglQuerySurface(display, surface, EGL_HEIGHT, &h);

  // 将EGL相关对象保存到引擎结构体中
  engine->display = display;
  engine->context = context;
  engine->surface = surface;
  engine->width = w;
  engine->height = h;
  engine->state.angle = 0;  // 初始化动画角度

  // 检查系统上的OpenGL信息
  // C++语法：初始化列表，创建包含OpenGL信息常量的列表
  auto opengl_info = {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS};
  
  // C++语法：范围for循环（C++11特性），遍历初始化列表
  for (auto name : opengl_info) {
    auto info = glGetString(name);  // 获取OpenGL信息字符串
    LOGI("OpenGL Info: %s", info);  // 输出到日志
  }
  
  // 初始化OpenGL状态
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);  // 设置透视校正提示为最快
  glEnable(GL_CULL_FACE);   // 启用面剔除，提高渲染性能
  glShadeModel(GL_SMOOTH);  // 设置平滑着色模式
  glDisable(GL_DEPTH_TEST); // 禁用深度测试（2D渲染不需要）

  return 0;  // 返回成功
}

/**
 * 销毁当前与显示设备关联的EGL上下文
 * 业务逻辑：清理EGL资源，释放显示上下文、表面等
 */
static void engine_term_display(Engine* engine) {
  // 检查显示设备是否有效
  if (engine->display != EGL_NO_DISPLAY) {
    // 取消当前上下文绑定，设置为无表面和无上下文
    eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    
    // 销毁渲染上下文
    if (engine->context != EGL_NO_CONTEXT) {
      eglDestroyContext(engine->display, engine->context);
    }
    
    // 销毁绘制表面
    if (engine->surface != EGL_NO_SURFACE) {
      eglDestroySurface(engine->display, engine->surface);
    }
    
    // 终止EGL显示设备
    eglTerminate(engine->display);
  }
  
  engine->Pause();  // 暂停引擎运行
  
  // 重置EGL相关成员为无效值
  engine->display = EGL_NO_DISPLAY;
  engine->context = EGL_NO_CONTEXT;
  engine->surface = EGL_NO_SURFACE;
}

/**
 * 处理下一个输入事件
 * C++语法：返回int32_t表示是否处理了事件（1=已处理，0=未处理）
 * 业务逻辑：捕获触摸事件，更新触摸坐标到引擎状态
 */
static int32_t engine_handle_input(android_app* app,
                                   AInputEvent* event) {
  // C++语法：强制类型转换，从void*转换为Engine*
  auto* engine = (Engine*)app->userData;
  
  // 检查是否为运动事件（触摸、鼠标等）
  if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
    // 获取第一个触摸点的X、Y坐标（索引0）
    engine->state.x = AMotionEvent_getX(event, 0);
    engine->state.y = AMotionEvent_getY(event, 0);
    return 1;  // 返回1表示事件已被处理
  }
  return 0;  // 返回0表示事件未被处理
}

/**
 * 处理下一个主要命令
 * 业务逻辑：响应Android系统的生命周期事件，管理窗口、焦点、状态保存等
 */
static void engine_handle_cmd(android_app* app, int32_t cmd) {
  auto* engine = (Engine*)app->userData;  // 获取引擎实例
  
  // C++语法：switch语句，根据命令类型执行不同操作
  switch (cmd) {
    case APP_CMD_SAVE_STATE:
      // 系统要求保存当前状态
      engine->app->savedState = malloc(sizeof(SavedState));  // 分配内存
      // C++语法：指针解引用和类型转换，将当前状态复制到保存区域
      *((SavedState*)engine->app->savedState) = engine->state;
      engine->app->savedStateSize = sizeof(SavedState);  // 设置保存状态大小
      break;
      
    case APP_CMD_INIT_WINDOW:
      // 窗口正在显示，准备渲染环境
      if (engine->app->window != nullptr) {
        engine_init_display(engine);  // 初始化EGL显示
      }
      break;
      
    case APP_CMD_TERM_WINDOW:
      // 窗口被隐藏或关闭，清理资源
      engine_term_display(engine);
      break;
      
    case APP_CMD_GAINED_FOCUS:
      // 应用获得焦点时，开始监控加速度计
      if (engine->accelerometerSensor != nullptr) {
        // 启用传感器
        ASensorEventQueue_enableSensor(engine->sensorEventQueue,
                                       engine->accelerometerSensor);
        // 设置采样率为60Hz（以微秒为单位）
        // C++语法：1000L中的L表示long类型字面量
        ASensorEventQueue_setEventRate(engine->sensorEventQueue,
                                       engine->accelerometerSensor,
                                       (1000L / 60) * 1000);
      }
      engine->Resume();  // 恢复引擎运行
      break;
      
    case APP_CMD_LOST_FOCUS:
      // 应用失去焦点时，停止监控加速度计以节省电池
      if (engine->accelerometerSensor != nullptr) {
        ASensorEventQueue_disableSensor(engine->sensorEventQueue,
                                        engine->accelerometerSensor);
      }
      engine->Pause();  // 暂停引擎运行
      break;
      
    default:
      break;  // 其他命令不处理
  }
}

/**
 * 传感器事件回调函数
 * C++语法：fd 和 events 是注释掉的参数名，表示未使用
 * 业务逻辑：处理加速度计传感器数据，输出到日志
 */
int OnSensorEvent(int /* fd */, int /* events */, void* data) {
  CHECK_NOT_NULL(data);  // 检查数据指针
  Engine* engine = reinterpret_cast<Engine*>(data);  // 转换为引擎指针

  CHECK_NOT_NULL(engine->accelerometerSensor);  // 检查传感器是否有效
  ASensorEvent event;  // 传感器事件结构体
  
  // 循环读取所有可用的传感器事件
  while (ASensorEventQueue_getEvents(engine->sensorEventQueue, &event, 1) > 0) {
    // 输出加速度计的X、Y、Z轴数据到日志
    LOGI("accelerometer: x=%f y=%f z=%f", event.acceleration.x,
         event.acceleration.y, event.acceleration.z);
  }

  // 根据文档说明：
  // 返回1表示继续接收回调，返回0表示从事件循环中注销此文件描述符和回调
  return 1;
}

/**
 * Android原生应用程序的主入口点
 * 业务逻辑：使用android_native_app_glue在独立线程中运行，
 * 拥有自己的事件循环，用于接收输入事件并执行其他操作
 */
void android_main(android_app* state) {
  // C++语法：{}初始化语法，创建Engine实例并零初始化
  Engine engine {};
  // C语法：memset将内存区域设置为0，确保所有成员都被初始化
  memset(&engine, 0, sizeof(engine));
  // 设置应用程序状态和回调函数
  state->userData = &engine;                    // 将引擎实例关联到应用状态
  state->onAppCmd = engine_handle_cmd;          // 设置命令处理回调
  state->onInputEvent = engine_handle_input;    // 设置输入事件处理回调
  engine.app = state;                          // 引擎保存应用状态引用
  // 准备监控加速度计
  engine.CreateSensorListener(OnSensorEvent);
  // 如果有之前保存的状态，则恢复它
  if (state->savedState != nullptr) {
    // C++语法：指针解引用和类型转换，恢复保存的状态
    engine.state = *(SavedState*)state->savedState;
  }
  // 主事件循环，直到应用被销毁
  while (!state->destroyRequested) {
    // 输入、传感器、更新/渲染逻辑都由回调驱动，
    // 所以不需要使用非阻塞轮询
    android_poll_source* source = nullptr;
    // C++语法：auto自动推导类型，ALooper_pollOnce等待事件
    // -1表示无限等待，reinterpret_cast进行指针类型转换
    auto result = ALooper_pollOnce(-1, nullptr, nullptr,
                                   reinterpret_cast<void**>(&source));
    // 检查轮询是否出错
    if (result == ALOOPER_POLL_ERROR) {
      fatal("ALooper_pollOnce returned an error");
    }
    // 如果有事件源，处理它
    if (source != nullptr) {
      source->process(state, source);  // 调用事件源的处理函数
    }
  }
  // 应用退出前清理EGL资源
  engine_term_display(&engine);
}
// END_INCLUDE(all)
