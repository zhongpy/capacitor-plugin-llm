# android/app/src/main/cpp/CMakeLists.txt
cmake_minimum_required(VERSION 3.18)
project(llama_jni)

add_library(llama_jni SHARED llama_jni.cpp)

set(LLAMA_SRC "E:/Work/Git/llama.cpp")
target_include_directories(llama_jni PRIVATE
        ${LLAMA_SRC}/build-android/stage/include          # 含 llama.h
)

# 链接 llama 共享库
add_library(llama SHARED IMPORTED)
set_target_properties(llama PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../jniLibs/arm64-v8a/libllama.so"
)

target_link_libraries(llama_jni llama log)
