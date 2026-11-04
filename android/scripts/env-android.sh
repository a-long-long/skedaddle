#!/bin/bash
# Skedaddle Android 构建环境（aarch64 Linux 宿主机）。
# 在 android/ 目录（或仓库任意位置）执行：
#     source android/scripts/env-android.sh
#
# 为什么需要它：本机是 ARM64 Linux，Android 工具链全部自装在 ~/tools/android
# （不进系统盘/系统包）。此外全局 ~/.gradle/init.gradle 会注入 mavenLocal，
# 与本项目 settings.gradle 的 FAIL_ON_PROJECT_REPOS 冲突，必须换独立
# GRADLE_USER_HOME。Windows / x86_64 Linux 机器用官方 NDK 标准流程，
# 不需要本脚本。

# 1) JDK 17（Temurin，/opt/jdk-17，环境变量见 /etc/profile.d/jdk17.sh；
#    这里显式设一遍，保证非交互 shell 也能用）
export JAVA_HOME=/opt/jdk-17

# 2) Android SDK / Gradle 自装位置
export ANDROID_HOME="$HOME/tools/android/sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"          # 部分 AGP 版本读这个
export GRADLE_USER_HOME="$HOME/tools/android/gradle-home"   # 避开全局 init.gradle 冲突

# 3) 桌面构建环境（protoc-c 宿主原生代码生成等；与仓库根 env.sh 同源）
SK_ARCH=$(gcc -dumpmachine 2>/dev/null || echo "$(uname -m)-linux-gnu")
export LD_LIBRARY_PATH="$HOME/.local/usr/lib/$SK_ARCH:$HOME/.local/lib:/usr/local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$HOME/.local/usr/bin:$HOME/.local/bin:$PATH"

# 排错用：关键项都应有输出
if [ "${SKEDADDLE_ENV_QUIET:-0}" != "1" ]; then
    printf 'JAVA_HOME   %s\n' "$(command -v java || echo 'NOT FOUND')"
    printf 'sdkmanager  %s\n' "$(command -v sdkmanager || echo "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager")"
    printf 'protoc-c    %s\n' "$(command -v protoc-c || echo 'NOT FOUND')"
    printf 'ninja       %s\n' "$(command -v ninja || echo 'NOT FOUND')"
fi
