# local-clang-android.toolchain.cmake
# aarch64 Linux 宿主机专用：用本机原生 LLVM/clang 24 交叉编译 Android arm64。
# 不使用官方 NDK 的 x86_64 宿主工具链（无法在本机运行），只用其 sysroot。
#
# 设计要点：
#  - CMAKE_SYSTEM_NAME 用 Linux 而非 Android：CMake 的 Android 平台模块会强制
#    探测 NDK 布局（与本方案的“sysroot-only”布局冲突）。Android 目标同为
#    ELF/Linux 内核，Linux 平台名即可正确产出 SHARED .so；交叉差异全部由
#    --target/--sysroot 编译器参数承担。
#  - 本机 clang 24 的 resource-dir 里没有运行时库（UOS 仓库版产物），因此自建
#    ~/tools/android/clang-res：include 符号链接到 clang 24 自带目录，
#    lib/aarch64-unknown-linux-android26/libclang_rt.builtins.a 取自 NDK r27b
#    （builtins 是目标代码，与宿主架构无关）。
#  - -unwindlib=none：clang 24 默认给 Android 链接行加 -l:libunwind.a，但 NDK
#    不带它，且 arm64 Android 的 unwinder（_Unwind_*）自 API 21 起就在 libc 里。

set(ANDROID_API "26" CACHE STRING "Android API level (minSdk)")
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(NDK_SYSROOT "$ENV{SK_NDK_SYSROOT}")
if(NOT NDK_SYSROOT)
  set(NDK_SYSROOT "$ENV{HOME}/tools/android/ndk-sysroot/toolchains/llvm/prebuilt/linux-x86_64/sysroot")
endif()
if(NOT EXISTS "${NDK_SYSROOT}/usr/include/jni.h")
  message(FATAL_ERROR "NDK sysroot 未找到：${NDK_SYSROOT}（先跑 android/scripts/setup-local-toolchain.sh）")
endif()

set(SK_CLANG_RES "$ENV{HOME}/tools/android/clang-res")
if(NOT EXISTS "${SK_CLANG_RES}/lib/aarch64-unknown-linux-android26/libclang_rt.builtins.a")
  message(FATAL_ERROR "未找到 ${SK_CLANG_RES}（先跑 android/scripts/setup-local-toolchain.sh）")
endif()

set(TRIPLE "aarch64-linux-android${ANDROID_API}")
set(CC_FLAGS "--target=${TRIPLE} --sysroot=${NDK_SYSROOT} -resource-dir=${SK_CLANG_RES}")
set(LD_FLAGS "${CC_FLAGS} -unwindlib=none")   # 仅链接行：编译期给该参数会有 warning

set(CMAKE_C_COMPILER   /usr/local/bin/clang)
set(CMAKE_CXX_COMPILER /usr/local/bin/clang++)

# 不能在 toolchain 文件里直接改 CMAKE_C_FLAGS（会被 project() 重置），
# 用 *_INIT 变量在首次探测编译器时注入：
set(CMAKE_C_FLAGS_INIT   "${CC_FLAGS}")
# JNI 桥只用 C 头文件、零 STL/异常，-nostdlib++ 省掉 libc++_shared.so 依赖
set(CMAKE_CXX_FLAGS_INIT "${CC_FLAGS} -nostdlib++")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${LD_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${LD_FLAGS}")

# 静态库要链进 .so，必须全 PIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 仅在 sysroot 内找库/头；可执行程序只在宿主找（protoc-c 是宿主原生工具）
set(CMAKE_FIND_ROOT_PATH "${NDK_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
