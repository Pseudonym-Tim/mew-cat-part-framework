cmake_minimum_required(VERSION 3.20)
project(MewCatPartFramework C)

if(NOT MSVC)
    message(FATAL_ERROR "MewCatPartFramework currently requires MSVC x64.")
endif()

add_library(MewCatPartFramework SHARED
    src/MewCatPartFramework.c
    MewCatPartFramework.def
)

target_compile_definitions(MewCatPartFramework PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
)
target_compile_options(MewCatPartFramework PRIVATE /W4 /WX-)
set_target_properties(MewCatPartFramework PROPERTIES
    OUTPUT_NAME "MewCatPartFramework"
)
