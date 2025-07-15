# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

- **Build all samples**: `./gradlew build` (Windows: `.\gradlew.bat build`)
- **Build specific sample**: `./gradlew :sample-name:build` (e.g., `./gradlew :hello-jni:app:build`)
- **View available tasks**: `./gradlew tasks`
- **View tasks for specific sample**: `./gradlew :sample-name:tasks` (e.g., `./gradlew :camera:basic:tasks`)
- **Clean build**: `./gradlew clean`

## Project Architecture

This is a multi-module Android NDK samples repository organized as a single Gradle project with multiple sample applications.

### Key Structure
- **Root project**: Contains shared Gradle configuration and build logic
- **Sample modules**: Each subdirectory (e.g., `hello-jni`, `camera/basic`, `audio-echo`) is an independent Android app module
- **build-logic/**: Contains Gradle convention plugins that define common build configurations
- **base/**: Shared C++ utilities and logging functions used across samples
- **teapots/common/**: Shared NDK helper libraries for OpenGL samples

### Sample Categories
- **Basic JNI**: `hello-jni`, `hello-jniCallback` - Basic C++/Java interop
- **Graphics**: `hello-gl2`, `gles3jni`, `hello-vulkan`, `teapots/*` - OpenGL ES and Vulkan rendering
- **Audio**: `native-audio`, `audio-echo`, `hello-oboe` - Audio processing and playback
- **Camera**: `camera/basic`, `camera/texture-view` - Camera2 API with native processing
- **Games**: `endless-tunnel`, `native-activity` - Game development patterns
- **Advanced**: `sanitizers`, `unit-test`, `prefab/*` - Development tools and dependency management

## Development Notes

### Building Samples
- All samples must be built from the root directory - individual sample directories cannot be opened directly in Android Studio
- Use the module selector in Android Studio to choose which sample to run
- Some samples have multiple build variants (debug/release, or additional variants like in `sanitizers`)

### NDK and CMake Integration
- Each sample's native code is built using CMake (CMakeLists.txt files in `src/main/cpp/`)
- SDK/NDK versions are centrally managed through build-logic convention plugins
- Third-party dependencies are managed through Prefab (see `prefab/*` samples for examples)

### Common Patterns
- JNI interfaces typically follow the pattern: `Java_package_Class_method` naming
- Native activities use `android_main()` as entry point
- OpenGL samples often use the NDK helper classes in `teapots/common/ndk_helper/`
- Audio samples demonstrate both low-level APIs and high-level Oboe library usage

## Testing
- Unit tests are demonstrated in the `unit-test` sample using Google Test
- Build the `unit-test:app` module to see native unit testing patterns
- Android instrumentation tests use `androidx.test.ext:junit-gtest` for running native tests

## Dependencies
- Versions are centrally managed in `gradle/libs.versions.toml`
- NDK third-party libraries (curl, openssl, googletest, jsoncpp) are available via Prefab
- Current AGP version: 8.10.0, targeting Android SDK 35