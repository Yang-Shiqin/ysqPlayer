# 设置最低CMake版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称和语言
project(BasicAvPlayer CXX)

# C++17标准
set(CMAKE_CXX_STANDARD 17)
# 设置编译器标志
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fsanitize=address -g")

# 查找必要的库
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)    # 跨平台查找库路径
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)

# 包含目录
include_directories(${FFMPEG_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})

# 源文件
set(SOURCES
    main.cc
    av_processor.cc
    av_SDL.cc
)

# 创建目标可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接FFmpeg和SDL2库
target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::FFMPEG
    SDL2::SDL2
)

# 清理构建文件
set_target_properties(${PROJECT_NAME} PROPERTIES
    CLEAN_DIRECT_OUTPUT 1
)
