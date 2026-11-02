cmake_minimum_required(VERSION 3.8)

project(BoardLab
    VERSION 0.1.0
    LANGUAGES CXX
)
set(PROJECT_DESCRIPTION "Qt Quick board hardware lab for Rockchip Linux SDKs")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Quick QuickControls2 Network Concurrent)
set(BOARDLAB_QT_PACKAGE Qt5)
set(BOARDLAB_QT_VERSION "${Qt5Core_VERSION}")
if(NOT BOARDLAB_QT_VERSION)
    set(BOARDLAB_QT_VERSION "${QT_VERSION}")
endif()
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
    "Use and install the source-tree RKNN runtime (legacy compatibility switch)"
    OFF
)
option(BOARDLAB_ENABLE_NPU_YOLO
    "Allow compiling NPU YOLO when runtime found"
    ON
)
option(BOARDLAB_REQUIRE_NPU_YOLO
    "Fail configuration when NPU YOLO dependencies are unavailable"
    OFF
)
set(BOARDLAB_RKNN_PROVIDER "AUTO" CACHE STRING
    "RKNN runtime provider: AUTO, SYSTEM, BUNDLED, or OFF"
)
set_property(CACHE BOARDLAB_RKNN_PROVIDER PROPERTY STRINGS AUTO SYSTEM BUNDLED OFF)
string(TOUPPER "${BOARDLAB_RKNN_PROVIDER}" BOARDLAB_RKNN_PROVIDER)
if(NOT BOARDLAB_RKNN_PROVIDER MATCHES "^(AUTO|SYSTEM|BUNDLED|OFF)$")
    message(FATAL_ERROR
        "Invalid BOARDLAB_RKNN_PROVIDER=${BOARDLAB_RKNN_PROVIDER}; use AUTO, SYSTEM, BUNDLED, or OFF"
    )
endif()
if(BOARDLAB_INSTALL_BUNDLED_RKNN_RUNTIME AND BOARDLAB_RKNN_PROVIDER STREQUAL "AUTO")
    set(BOARDLAB_RKNN_PROVIDER "BUNDLED")
endif()

# The NPU demonstration remains a normal BoardLab page on every host, but it
# only compiles the RKNN/OpenCV worker on 64-bit ARM targets where all runtime
# pieces are present.  Desktop builds therefore stay useful and fail closed
# instead of accidentally linking the AArch64 RKNN library from AI NVR.
set(BOARDLAB_NPU_YOLO_SOURCES)
set(BOARDLAB_NPU_YOLO_ENABLED OFF)
set(BOARDLAB_RKNN_RUNTIME_IS_BUNDLED OFF)
set(BOARDLAB_RKNN_INCLUDE_DIR "")
set(BOARDLAB_RKNN_RUNTIME_LIBRARY "")
set(BOARDLAB_RKNN_BUNDLED_RUNTIME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/lib/aarch64"
)
if(BOARDLAB_ENABLE_NPU_YOLO
   AND NOT BOARDLAB_RKNN_PROVIDER STREQUAL "OFF"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
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

    # A Rockchip SDK's rknpu2 package is the ABI authority for its kernel
    # driver. AUTO and SYSTEM therefore search the toolchain/sysroot only.
    # BUNDLED is explicit and isolated so one SDK's librknnrt cannot silently
    # leak into another SDK or SoC build.
    if(BOARDLAB_RKNN_PROVIDER STREQUAL "BUNDLED")
        find_path(BOARDLAB_RKNN_BUNDLED_INCLUDE_DIR
            NAMES rknn_api.h
            PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/include"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
        )
        find_library(BOARDLAB_RKNN_BUNDLED_RUNTIME_LIBRARY
            NAMES rknnrt
            PATHS "${BOARDLAB_RKNN_BUNDLED_RUNTIME_DIR}"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
        )
        set(BOARDLAB_RKNN_INCLUDE_DIR "${BOARDLAB_RKNN_BUNDLED_INCLUDE_DIR}")
        set(BOARDLAB_RKNN_RUNTIME_LIBRARY "${BOARDLAB_RKNN_BUNDLED_RUNTIME_LIBRARY}")
        if(BOARDLAB_RKNN_BUNDLED_INCLUDE_DIR AND BOARDLAB_RKNN_BUNDLED_RUNTIME_LIBRARY)
            set(BOARDLAB_RKNN_RUNTIME_IS_BUNDLED ON)
        endif()
    else()
        find_path(BOARDLAB_RKNN_SYSTEM_INCLUDE_DIR
            NAMES rknn_api.h
            PATH_SUFFIXES rknn rknpu2
        )
        find_library(BOARDLAB_RKNN_SYSTEM_RUNTIME_LIBRARY NAMES rknnrt)
        set(BOARDLAB_RKNN_INCLUDE_DIR "${BOARDLAB_RKNN_SYSTEM_INCLUDE_DIR}")
        set(BOARDLAB_RKNN_RUNTIME_LIBRARY "${BOARDLAB_RKNN_SYSTEM_RUNTIME_LIBRARY}")
    endif()
    if(BOARDLAB_OPENCV_FOUND AND BOARDLAB_RKNN_INCLUDE_DIR AND BOARDLAB_RKNN_RUNTIME_LIBRARY)
        set(BOARDLAB_NPU_YOLO_ENABLED ON)
        list(APPEND BOARDLAB_NPU_YOLO_SOURCES
            src/npu/detector/npu_yolo_detector.cpp
            src/npu/detector/npu_yolo_detector.h
            src/npu/detector/npu_yolo_postprocess.cpp
            src/npu/detector/npu_yolo_postprocess.h
        )
        message(STATUS "BoardLab YOLOv8 NPU demo: enabled (${BOARDLAB_RKNN_PROVIDER} RKNN provider)")
    else()
        set(BOARDLAB_NPU_UNAVAILABLE_REASON
            "OpenCV components, RKNN header, or ${BOARDLAB_RKNN_PROVIDER} RKNN runtime missing"
        )
        message(STATUS "BoardLab YOLOv8 NPU demo: unavailable at build time (${BOARDLAB_NPU_UNAVAILABLE_REASON})")
    endif()
elseif(NOT BOARDLAB_ENABLE_NPU_YOLO)
    set(BOARDLAB_NPU_UNAVAILABLE_REASON
        "disabled by BOARDLAB_ENABLE_NPU_YOLO=OFF"
    )
    message(STATUS "BoardLab YOLOv8 NPU demo: disabled (BOARDLAB_ENABLE_NPU_YOLO=OFF)")
elseif(BOARDLAB_RKNN_PROVIDER STREQUAL "OFF")
    set(BOARDLAB_NPU_UNAVAILABLE_REASON
        "disabled by BOARDLAB_RKNN_PROVIDER=OFF"
    )
    message(STATUS "BoardLab YOLOv8 NPU demo: disabled (BOARDLAB_RKNN_PROVIDER=OFF)")
else()
    set(BOARDLAB_NPU_UNAVAILABLE_REASON
        "RKNN inference is supported only by this project on 64-bit ARM targets"
    )
    message(STATUS "BoardLab YOLOv8 NPU demo: unavailable at build time (${BOARDLAB_NPU_UNAVAILABLE_REASON})")
endif()

if(BOARDLAB_REQUIRE_NPU_YOLO AND NOT BOARDLAB_NPU_YOLO_ENABLED)
    message(FATAL_ERROR
        "BoardLab NPU YOLO was required but is unavailable: ${BOARDLAB_NPU_UNAVAILABLE_REASON}"
    )
endif()

add_executable(boardlab
    src/main.cpp
    src/singleinstance.cpp
    src/singleinstance.h
    src/boardprofile.cpp
    src/boardprofile.h
    src/hardwareinventory.cpp
    src/hardwareinventory.h
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
        ${BOARDLAB_QT_PACKAGE}::Core
        ${BOARDLAB_QT_PACKAGE}::Quick
        ${BOARDLAB_QT_PACKAGE}::QuickControls2
        ${BOARDLAB_QT_PACKAGE}::Network
        ${BOARDLAB_QT_PACKAGE}::Concurrent
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
    if(BOARDLAB_RKNN_RUNTIME_IS_BUNDLED)
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
    config/board-profile.schema.json
    config/board-profile.example.json
    DESTINATION share/boardlab/config
)

install(FILES
    config/board-profile.schema.json
    config/board-profile.example.json
    DESTINATION share/doc/boardlab
)

file(GLOB BOARDLAB_RKNN_MODELS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/npu/models/*.rknn"
)
if(BOARDLAB_RKNN_MODELS)
    install(FILES ${BOARDLAB_RKNN_MODELS}
        DESTINATION share/boardlab/npu/models
    )
endif()
install(FILES assets/npu/videos/person-bicycle-car-detection.mp4
    DESTINATION share/boardlab/npu/videos
)
install(FILES assets/npu/labels/coco_80_labels.txt
    DESTINATION share/boardlab/npu
)

if(BOARDLAB_NPU_YOLO_ENABLED AND BOARDLAB_RKNN_RUNTIME_IS_BUNDLED)
    install(FILES "${BOARDLAB_RKNN_RUNTIME_LIBRARY}"
        DESTINATION lib/boardlab
    )
endif()

message(STATUS "BoardLab build summary:")
message(STATUS "  CMake: ${CMAKE_VERSION}")
message(STATUS "  Qt: ${BOARDLAB_QT_PACKAGE} ${BOARDLAB_QT_VERSION}")
message(STATUS "  Target processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Cross compiling: ${CMAKE_CROSSCOMPILING}")
if(BOARDLAB_GSTREAMER_APPSINK_FOUND)
    message(STATUS "  GStreamer appsink: ON")
else()
    message(STATUS "  GStreamer appsink: OFF")
endif()
message(STATUS "  RKNN provider: ${BOARDLAB_RKNN_PROVIDER}")
message(STATUS "  NPU YOLO: ${BOARDLAB_NPU_YOLO_ENABLED}")
