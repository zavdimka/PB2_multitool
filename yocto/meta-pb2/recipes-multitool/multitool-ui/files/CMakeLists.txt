cmake_minimum_required(VERSION 3.21)
project(multitool-ui LANGUAGES CXX)

find_package(Slint REQUIRED)

add_executable(multitool-ui main.cpp)
target_compile_features(multitool-ui PRIVATE cxx_std_17)
target_compile_options(multitool-ui PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(multitool-ui PRIVATE Slint::Slint)
slint_target_sources(multitool-ui appwindow.slint)

install(TARGETS multitool-ui RUNTIME DESTINATION bin)