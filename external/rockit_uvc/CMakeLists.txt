cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
PROJECT(rockit_uvc)

include_directories(${CMAKE_SYSROOT}/usr/include/)
include_directories(${CMAKE_SYSROOT}/usr/include/uvc)

set(SOURCE
    rockit_uvc.c
)
ADD_EXECUTABLE(rockit_uvc ${SOURCE})
target_link_libraries(rockit_uvc rkuvc rockit)

install(TARGETS rockit_uvc DESTINATION bin)
