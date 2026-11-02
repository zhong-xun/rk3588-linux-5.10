cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)
PROJECT(uvc_app)

include_directories(uvc)

set(LIB_SOURCE
    uvc/uvc-gadget.c
    uvc/uvc_video.cpp
    uvc/yuv.c
    uvc/uvc_control.c
    uvc/uvc_encode.cpp
    uvc/uevent.c
    uvc/drm.c
)
add_library(rkuvc SHARED ${LIB_SOURCE})
target_link_libraries(rkuvc pthread drm)

set(SOURCE
    main.c
    ${LIB_SOURCE}
)

ADD_EXECUTABLE(uvc_app ${SOURCE})
target_link_libraries(uvc_app pthread drm)

install(TARGETS rkuvc DESTINATION lib)
install(DIRECTORY ./uvc DESTINATION include
        FILES_MATCHING PATTERN "*.h")

install(TARGETS uvc_app DESTINATION bin)

if(RK3588)
install(FILES rk3588/usb_config.sh DESTINATION bin
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
elseif(RK3576)
install(FILES rk3576/usb_config.sh DESTINATION bin
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
else()
install(FILES uvc_config.sh DESTINATION bin
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
endif()
