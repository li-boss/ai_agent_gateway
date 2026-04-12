# 1. 指定最低的 CMake 版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 定义项目名称和使用的语言 (CXX 代表 C++)
project(AIAgentEngine VERSION 1.0 LANGUAGES CXX)

# 3. 强制使用 C++17 标准 (手写线程池用到的 std::future 和可变参数模板需要较新的标准)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 4. 寻找 Linux 系统的多线程库
# 敲黑板：在 WSL/Ubuntu 下，C++ 的 std::thread 底层依赖系统的 pthread 库，不加这个编译会报 undefined reference
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 5. 指定头文件搜索路径
# 这样你在 src/main.cpp 里就可以直接写 #include "core/ThreadPool.h"，不用写长长的一串相对路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 6. 定义你要生成的最终可执行文件，以及它依赖的源文件
add_executable(AIAgentGateway src/main.cpp)

# 7. 将刚刚找到的多线程库链接到我们的主程序上
target_link_libraries(AIAgentGateway PRIVATE Threads::Threads)

# 查找系统的 OpenSSL 库
find_package(OpenSSL REQUIRED)

# 核心修改：将 Threads 和 OpenSSL 库链接到你的网关程序上
target_link_libraries(AIAgentGateway PRIVATE Threads::Threads OpenSSL::SSL OpenSSL::Crypto)