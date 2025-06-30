#
# Copyright 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
cmake_minimum_required(VERSION 3.22.1)
project(hello-oboe LANGUAGES CXX)

# 添加托管在 https://maven.google.com/web/index.html 的 oboe 预发布库
# 位于 com.google.oboe:oboe 下。有关 oboe 预构建库的文档，请参考
# https://github.com/google/oboe/blob/master/docs/GettingStarted.md#option-1-using-pre-built-binaries-and-headers
find_package(oboe REQUIRED CONFIG)

# 使用 oboe 库构建应用程序
add_library(${PROJECT_NAME} SHARED  hello-oboe.cpp)
target_link_libraries(${PROJECT_NAME} oboe::oboe android log)

# 启用优化标志：如果在源码级调试时遇到问题，
# 请禁用 -Ofast（并进行调试），调试完成后重新启用。
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Werror "$<$<CONFIG:RELEASE>:-Ofast>")
