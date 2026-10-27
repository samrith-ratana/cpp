cmake_minimum_required(VERSION 3.15)
project(cpp_vercel CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

add_executable(server main.cpp)
target_link_libraries(server PRIVATE httplib::httplib)
target_compile_options(server PRIVATE -pthread)