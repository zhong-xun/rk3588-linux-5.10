cmake_minimum_required(VERSION 3.16)

project(BoardLab
    VERSION 0.1.0
    DESCRIPTION "Qt Quick board hardware lab for Rockchip Buildroot, Ubuntu and Debian"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Quick QuickControls2 Network Concurrent)
find_package(Threads REQUIRED)

# BoardLab target-image integrations provide the GStreamer app/video development
# interfaces. Keep this optional for host-only UI workstations, where GStreamer
# headers are commonly absent; the QML type then remains available but reports
# a clear unavailable status instead of falling back to Qt CameraBin.
find_package(PkgConfig QUIET)
set(BOARDLAB_GSTREAMER_APPSINK_FOUND FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(BOARDLAB_GSTREAMER_APPSINK QUIET IMPORTED_TARGET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
    )
endif()
option(BOARDLAB_REQUIRE_GSTREAMER_APPSINK
    "Fail configuration when the GStreamer appsink preview is unavailable"
    OFF
)
if(BOARDLAB_REQUIRE_GSTREAMER_APPSINK AND NOT BOARDLAB_GSTREAMER_APPSINK_FOUND)
    message(FATAL_ERROR
        "BoardLab requires GStreamer appsink support; install the GStreamer core/app/video development interfaces"
    )
endif()

option(BOARDLAB_INSTALL_BUNDLED_RKNN_RUNTIME
    "Install third_party librknnrt.so next to the binary"
    ON
)
option(BOARDLAB_ENABLE_NPU_YOLO
    "Allow compiling NPU YOLO when runtime found"
    ON
)

# The NPU demonstration remains a normal BoardLab page on every host, but it
# only compiles the RKNN/OpenCV worker on 64-bit ARM targets where all runtime
# pieces are present.  Desktop builds therefore stay useful and fail closed
# instead of accidentally linking the AArch64 RKNN library from AI NVR.
set(BOARDLAB_NPU_YOLO_SOURCES)
set(BOARDLAB_NPU_YOLO_ENABLED OFF)
set(BOARDLAB_RKNN_BUNDLED_RUNTIME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/lib/aarch64"
)
if(BOARDLAB_ENABLE_NPU_YOLO AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    # Avoid the libopencv-dev meta package: its optional highgui development
    # stack pulls an exact-version FFmpeg development ABI that conflicts with
    # Rockchip's packaged FFmpeg runtime.  The YOLO worker needs only these
    # four component headers and libraries.
    set(BOARDLAB_OPENCV_FOUND TRUE)
    set(BOARDLAB_OPENCV_LIBS)
    find_path(BOARDLAB_OPENCV_INCLUDE_DIR
        NAMES opencv2/core.hpp
        PATH_SUFFIXES opencv4
    )
    if(NOT BOARDLAB_OPENCV_INCLUDE_DIR)
        set(BOARDLAB_OPENCV_FOUND FALSE)
    endif()
    foreach(component core imgproc imgcodecs videoio)
        string(TOUPPER "${component}" component_upper)
        set(component_library_var "BOARDLAB_OPENCV_${component_upper}_LIBRARY")
        find_library(${component_library_var} NAMES "opencv_${component}")
        if(NOT ${component_library_var})
            set(BOARDLAB_OPENCV_FOUND FALSE)
        else()
            list(APPEND BOARDLAB_OPENCV_LIBS "${${component_library_var}}")
        endif()
    endforeach()

    # Prefer find_path/find_library (respect CMAKE_FIND_ROOT_PATH).  Host-only
    # paths such as multichannel_ai_nvr and bare /usr are skipped when
    # cross-compiling.  third_party is only considered when the bundled runtime
    # install option is ON.
    set(BOARDLAB_RKNN_INCLUDE_HINTS)
    set(BOARDLAB_RKNN_LIBRARY_HINTS)
    if(BOARDLAB_INSTALL_BUNDLED_RKNN_RUNTIME)
        list(APPEND BOARDLAB_RKNN_INCLUDE_HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/include
        )
        list(APPEND BOARDLAB_RKNN_LIBRARY_HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/lib/aarch64
        )
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        list(APPEND BOARDLAB_RKNN_INCLUDE_HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/../multichannel_ai_nvr/include
            /usr/include
            /usr/local/include
            /usr/include/rknn
            /usr/local/include/rknn
        )
        list(APPEND BOARDLAB_RKNN_LIBRARY_HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/../multichannel_ai_nvr/lib
            /usr/lib
            /usr/local/lib
        )
    endif()

    find_path(BOARDLAB_RKNN_INCLUDE_DIR
        NAMES rknn_api.h
        PATHS ${BOARDLAB_RKNN_INCLUDE_HINTS}
        PATH_SUFFIXES rknn
    )
    find_library(BOARDLAB_RKNN_RUNTIME_LIBRARY
        NAMES rknnrt librknnrt.so
        PATHS ${BOARDLAB_RKNN_LIBRARY_HINTS}
    )
    if(BOARDLAB_OPENCV_FOUND AND BOARDLAB_RKNN_INCLUDE_DIR AND BOARDLAB_RKNN_RUNTIME_LIBRARY)
        set(BOARDLAB_NPU_YOLO_ENABLED ON)
        list(APPEND BOARDLAB_NPU_YOLO_SOURCES
            src/npu/detector/npu_yolo_detector.cpp
            src/npu/detector/npu_yolo_detector.h
            src/npu/detector/npu_yolo_postprocess.cpp
            src/npu/detector/npu_yolo_postprocess.h
        )
        message(STATUS "BoardLab YOLOv8 NPU demo: enabled")
    else()
        message(STATUS "BoardLab YOLOv8 NPU demo: unavailable at build time (OpenCV components, RKNN header, or RKNN runtime missing)")
    endif()
elseif(NOT BOARDLAB_ENABLE_NPU_YOLO)
    message(STATUS "BoardLab YOLOv8 NPU demo: disabled (BOARDLAB_ENABLE_NPU_YOLO=OFF)")
endif()

add_executable(boardlab
    src/main.cpp
    src/boardprofile.cpp
    src/boardprofile.h
    src/environmentmonitor.cpp
    src/environmentmonitor.h
    src/gpubenchmark.cpp
    src/gpubenchmark.h
    src/gstvideopreviewitem.cpp
    src/gstvideopreviewitem.h
    src/camerapreviewprofile.h
    src/webcamerapreview.cpp
    src/webcamerapreview.h
    src/keydevicediscovery.cpp
    src/keydevicediscovery.h
    src/keymonitor.cpp
    src/keymonitor.h
    src/boardbackend.cpp
    src/boardbackend.h
    src/localwebserver.cpp
    src/localwebserver.h
    src/qrimageprovider.cpp
    src/qrimageprovider.h
    src/resourcemonitor.cpp
    src/resourcemonitor.h
    src/systeminfo.cpp
    src/systeminfo.h
    src/wifimanager.cpp
    src/wifimanager.h
    src/npu/npu_yolo_demo.cpp
    src/npu/npu_yolo_demo.h
    src/npu/npu_load_reader.cpp
    src/npu/npu_load_reader.h
    ${BOARDLAB_NPU_YOLO_SOURCES}
    qml/qml.qrc
    web/web.qrc
)

target_link_libraries(boardlab
    PRIVATE
        Qt5::Core
        Qt5::Quick
        Qt5::QuickControls2
        Qt5::Network
        Qt5::Concurrent
        Threads::Threads
)

if(BOARDLAB_GSTREAMER_APPSINK_FOUND)
    target_link_libraries(boardlab PRIVATE PkgConfig::BOARDLAB_GSTREAMER_APPSINK)
    target_compile_definitions(boardlab PRIVATE BOARDLAB_HAS_GSTREAMER_APPSINK=1)
    message(STATUS "BoardLab embedded GStreamer appsink preview: enabled")
else()
    message(STATUS "BoardLab embedded GStreamer appsink preview: unavailable (host fallback)")
endif()

if(BOARDLAB_NPU_YOLO_ENABLED)
    target_include_directories(boardlab PRIVATE
        ${BOARDLAB_OPENCV_INCLUDE_DIR}
        ${BOARDLAB_RKNN_INCLUDE_DIR}
    )
    target_link_libraries(boardlab PRIVATE
        ${BOARDLAB_OPENCV_LIBS}
        ${BOARDLAB_RKNN_RUNTIME_LIBRARY}
    )
    target_compile_definitions(boardlab PRIVATE BOARDLAB_HAS_NPU_YOLO=1)

    # Keep direct build-tree and installed invocations on the RKNN runtime
    # shipped with BoardLab when the bundled runtime option is enabled.  The
    # installed launcher retains LD_LIBRARY_PATH for legacy deployments, but the
    # binaries must not depend on that wrapper.
    if(BOARDLAB_INSTALL_BUNDLED_RKNN_RUNTIME)
        set_target_properties(boardlab PROPERTIES
            BUILD_RPATH "${BOARDLAB_RKNN_BUNDLED_RUNTIME_DIR}"
            INSTALL_RPATH "$ORIGIN/../lib/boardlab"
        )
    endif()
endif()

target_compile_definitions(boardlab
    PRIVATE
        QT_DEPRECATED_WARNINGS
        QT_DISABLE_DEPRECATED_BEFORE=0x050C00
)

install(TARGETS boardlab
    RUNTIME DESTINATION bin
)

install(FILES assets/icons/boardlab.svg
    DESTINATION share/icons/hicolor/scalable/apps
)

install(FILES
    assets/npu/models/yolov8n_rk3562.rknn
    assets/npu/models/yolov8n_rk3568.rknn
    assets/npu/models/yolov8n_rk3576.rknn
    assets/npu/models/yolov8n_rk3588.rknn
    # Keep the former RK3588-only asset for installed images that still invoke
    # an older BoardLab binary. New code always selects the explicit filename.
    assets/npu/models/yolov8.rknn
    DESTINATION share/boardlab/npu/models
)
install(FILES assets/npu/videos/person-bicycle-car-detection.mp4
    DESTINATION share/boardlab/npu/videos
)
install(FILES assets/npu/labels/coco_80_labels.txt
    DESTINATION share/boardlab/npu
)

if(BOARDLAB_NPU_YOLO_ENABLED AND BOARDLAB_INSTALL_BUNDLED_RKNN_RUNTIME)
    install(FILES third_party/rknn/lib/aarch64/librknnrt.so
        DESTINATION lib/boardlab
    )
endif()
