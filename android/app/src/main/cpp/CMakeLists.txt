cmake_minimum_required(VERSION 3.18)
project(skedaddle_android C CXX)

# skedaddle 源码根：本文件固定位于 android/app/src/main/cpp，向上 5 级即仓库根。
# 也可用 -DSKEDADDLE_SRC_ROOT=... 覆盖（由 gradle 传绝对路径最稳）。
if(NOT SKEDADDLE_SRC_ROOT)
  set(SKEDADDLE_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../")
endif()
get_filename_component(SKEDADDLE_SRC_ROOT "${SKEDADDLE_SRC_ROOT}" ABSOLUTE)
set(SK_SRC "${SKEDADDLE_SRC_ROOT}/src")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# ---------------------------------------------------------------------------
# protobuf-c 代码生成（host 端 protoc-c，不交叉编译；与 desktop 一致）
# ---------------------------------------------------------------------------
find_program(PROTOC_C NAMES protoc-c)
if(NOT PROTOC_C)
  message(FATAL_ERROR
    "host 端 protoc-c 未找到（用于生成 osmformat/fileformat 的 C binding）。"
    "请在构建机安装 protobuf-c 工具（或 source env.sh）。")
endif()
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GEN_DIR})
set(PROTO_SRCS ${GEN_DIR}/osmformat.pb-c.c ${GEN_DIR}/fileformat.pb-c.c)
set(PROTO_HDRS ${GEN_DIR}/osmformat.pb-c.h ${GEN_DIR}/fileformat.pb-c.h)
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES GENERATED TRUE)
add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
  COMMAND ${PROTOC_C} --c_out=${GEN_DIR} -I ${SKEDADDLE_SRC_ROOT}/proto
          ${SKEDADDLE_SRC_ROOT}/proto/osmformat.proto
          ${SKEDADDLE_SRC_ROOT}/proto/fileformat.proto
  DEPENDS ${SKEDADDLE_SRC_ROOT}/proto/osmformat.proto
          ${SKEDADDLE_SRC_ROOT}/proto/fileformat.proto
  COMMENT "生成 protobuf-c binding (host)")

# ---------------------------------------------------------------------------
# 第三方：NDK 不自带 sqlite3 与 protobuf-c 运行时，需 vendor 进 cpp/thirdparty/
#   cpp/thirdparty/sqlite3/sqlite3.c + sqlite3.h
#   cpp/thirdparty/protobuf-c/protobuf-c.c + protobuf-c.h
# 两文件存在就编进去；缺失则给 WARNING（链接阶段才会真正失败，便于提前提示）。
# 取源命令见 docs/android.md。
# ---------------------------------------------------------------------------
set(THIRDPARTY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
set(SQLITE3_SRC "${THIRDPARTY}/sqlite3/sqlite3.c")
set(PROTOC_C_SRC "${THIRDPARTY}/protobuf-c/protobuf-c.c")
set(EXTRA_TP "")
if(EXISTS "${SQLITE3_SRC}")
  list(APPEND EXTRA_TP "${SQLITE3_SRC}")
  include_directories("${THIRDPARTY}/sqlite3")
  add_definitions(-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1)
else()
  message(WARNING "未找到 cpp/thirdparty/sqlite3/sqlite3.c —— 链接会因 sqlite3_* 缺失失败。"
                   "请从 https://www.sqlite.org/ 取 amalgamation 放入。")
endif()
if(EXISTS "${PROTOC_C_SRC}")
  list(APPEND EXTRA_TP "${PROTOC_C_SRC}")
  # 生成的 pb-c.h 以 <protobuf-c/protobuf-c.h> 引头，须把 thirdparty 父目录
  # 放进 include 路径；同一目录下的 protobuf-c.c 用 "protobuf-c.h" 相对引用也兼容。
  include_directories("${THIRDPARTY}")
else()
  message(WARNING "未找到 cpp/thirdparty/protobuf-c/protobuf-c.c —— 请 vendor protobuf-c 运行时源码。")
endif()

# ---------------------------------------------------------------------------
# skedaddle 核心源码清单（与 desktop CMakeLists.txt 同步；去掉离屏 CLI 的
# offscreen_shot.c —— Android 走 JNI 直接调 app_init/app_render_to_buf）
# ---------------------------------------------------------------------------
set(PBF_SRCS
  ${SK_SRC}/pbf/pbf_stream.c ${SK_SRC}/pbf/pbf_block.c
  ${PROTO_SRCS} ${PROTO_HDRS})
set(MAP_SRCS
  ${SK_SRC}/map/road_tags.c ${SK_SRC}/map/binfmt.c ${SK_SRC}/map/graph_build.c
  ${SK_SRC}/map/pbfmap.c ${SK_SRC}/map/mapset.c ${SK_SRC}/map/namepool.c
  ${SK_SRC}/map/autobuild.c ${SK_SRC}/map/coverage.c ${SK_SRC}/map/map_view.c
  ${SK_SRC}/map/tileidx.c ${SK_SRC}/map/gpx.c)
set(ROUTE_SRCS
  ${SK_SRC}/route/st.c ${SK_SRC}/route/sew.c ${SK_SRC}/route/restr.c
  ${SK_SRC}/route/cost.c ${SK_SRC}/route/lmdist.c ${SK_SRC}/route/result.c
  ${SK_SRC}/route/route.c ${SK_SRC}/route/lmtable.c ${SK_SRC}/route/revidx.c
  ${SK_SRC}/route/maneuver.c ${SK_SRC}/route/navsim.c ${SK_SRC}/route/voice.c)
set(SEARCH_SRCS ${SK_SRC}/search/search.c)
set(RENDER_SRCS
  ${SK_SRC}/render/paint.c ${SK_SRC}/render/raster.c ${SK_SRC}/render/navdraw.c
  ${SK_SRC}/render/font.c ${SK_SRC}/render/labels.c ${SK_SRC}/render/trackdraw.c)
set(UI_SRCS
  ${SK_SRC}/ui/window.c ${SK_SRC}/ui/overlay.c ${SK_SRC}/ui/voice_wire.c
  ${SK_SRC}/ui/chrome.c ${SK_SRC}/ui/audit.c)
set(UTILS_SRCS
  ${SK_SRC}/utils/plat.c ${SK_SRC}/utils/geo.c ${SK_SRC}/utils/datum.c
  ${SK_SRC}/i18n/i18n.c ${SK_SRC}/utils/strpool.c ${SK_SRC}/utils/mem.c
  ${SK_SRC}/settings/settings.c)

# 整核心编进一个 STATIC；再链成 SHARED 的 libskedaddle.so（JNI 所在）。
# 单一 STATIC 库避开了 desktop 那种多库链接顺序问题，NDK 下最省心。
add_library(skedaddle_core STATIC
  ${PBF_SRCS} ${MAP_SRCS} ${ROUTE_SRCS} ${SEARCH_SRCS} ${RENDER_SRCS}
  ${UI_SRCS} ${UTILS_SRCS} ${EXTRA_TP})
target_include_directories(skedaddle_core PUBLIC ${SK_SRC} ${GEN_DIR})
# 核心需要 zlib（NDK 自带 -lz）、数学库；log 给 __android_log_print 调试用。
target_link_libraries(skedaddle_core PUBLIC log z m)

# ---------------------------------------------------------------------------
# JNI 桥：本目录的 skedaddle_jni.cpp，编进 SHARED libskedaddle.so
# ---------------------------------------------------------------------------
add_library(skedaddle SHARED skedaddle_jni.cpp)
target_include_directories(skedaddle PRIVATE ${SK_SRC})
target_link_libraries(skedaddle PRIVATE skedaddle_core log)
set_target_properties(skedaddle PROPERTIES OUTPUT_NAME "skedaddle")

# 默认最小 Android API（与 build.gradle 的 minSdk 对齐；CMake 侧也需要）
if(NOT ANDROID_PLATFORM)
  set(ANDROID_PLATFORM android-26)
endif()
