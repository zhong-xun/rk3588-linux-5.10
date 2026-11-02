if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

cmake_minimum_required(VERSION 2.8...3.22)

PROJECT(IspFec)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(BuildOptions)
include(CompileOptions)
include(GNUInstallDirs)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

set(RK_ISPFEC_SOURCE_DIR ${PROJECT_SOURCE_DIR})
message("rkIspFec src path" ${RK_ISPFEC_SOURCE_DIR})

add_compile_options(-Os)
add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
set(RK_ISPFEC_INSTALLED_DIR "${RK_ISPFEC_SOURCE_DIR}/build/linux/output/${ARCH}/installed" CACHE STRING "ISPFEC library installed dir")
set(RK_ISPFEC_LIB_DIR ${RK_ISPFEC_INSTALLED_DIR}/lib)
message("rkIspFec lib path" ${RK_ISPFEC_LIB_DIR})

include_directories(
    ./
    include/
    include_prv/
    )

set (IspFec_SRC
	src/rk_ispfec.cpp
	src/RkIspFecHw.cpp
	src/RkIspFecHwMgr.cpp
	)

set(ISP_FEC_STATIC_LIB_ARS)
set(STATIC_LIB_PREFIX $<TARGET_FILE:)
set(STATIC_LIB_SUFFIX >)

if(RK_GDC_GEN_MESH_ONLINE)
	list(APPEND IspFec_SRC
	     src/gen_mesh/genMesh.cpp
	     src/RkIspFecGenMesh.cpp
	     )

    set(GENMESH_LIBS genMeshLib)
    if(ARCH STREQUAL "aarch64")
        set(GENMESH_LIBS_DIR
            ${PROJECT_SOURCE_DIR}/src/gen_mesh/linux/genMesh_static_64bit/
            )
    else()
        set(GENMESH_LIBS_DIR
            ${PROJECT_SOURCE_DIR}/src/gen_mesh/linux/genMesh_static_32bit/
            )
    endif()

    list(APPEND ISP_FEC_STATIC_LIB_ARS ${GENMESH_LIBS_DIR}/libgenMeshLib.a)
    add_compile_options(-DGENMESH_ONLINE)
endif()

list(APPEND DIR_SRCS ${AFDEMO_DIR_SRCS})

SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/all_lib/${CMAKE_BUILD_TYPE})
SET(CMAKE_LIBRARY_PATH ${LIBRARY_OUTPUT_PATH})

#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
#set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
message(${CMAKE_LIBRARY_PATH})
message(${LIBRARY_OUTPUT_PATH})

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

link_directories("${RK_ISPFEC_LIB_DIR}" "${GENMESH_LIBS_DIR}")
add_library(IspFec SHARED ${IspFec_SRC})

target_link_libraries(IspFec
	PRIVATE
    ${GENMESH_LIBS})

add_library(IspFec_static STATIC ${IspFec_SRC})
set_target_properties(IspFec_static PROPERTIES FOLDER "IspFec")
set_target_properties(IspFec_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

list(APPEND ISP_FEC_STATIC_LIB_ARS
    ${STATIC_LIB_PREFIX}IspFec_static${STATIC_LIB_SUFFIX}
    )


#build static libIspFec_static.a
foreach(STATIC_LIB_AR ${ISP_FEC_STATIC_LIB_ARS})
        add_custom_command (TARGET IspFec_static POST_BUILD
        COMMAND ${CMAKE_AR} x ${STATIC_LIB_AR}
        )
endforeach()

add_custom_command (TARGET IspFec_static POST_BUILD
        COMMAND ${CMAKE_AR} rcs ${CMAKE_LIBRARY_PATH}/libIspFec_static.a *.o
        COMMAND ${CMAKE_STRIP} --strip-debug ${CMAKE_LIBRARY_PATH}/libIspFec_static.a
        COMMAND rm *.o
        )

install(DIRECTORY include/ DESTINATION include/IspFec FILES_MATCHING PATTERN "*.h")
install(FILES ${CMAKE_LIBRARY_PATH}/libIspFec.so DESTINATION lib)
install(FILES ${CMAKE_LIBRARY_PATH}/libIspFec_static.a DESTINATION lib RENAME libIspFec.a)

if (ENABLE_SAMPLE_ISPFEC)
    if (${CMAKE_SYSTEM_NAME} EQUAL "")
        set(OS "linux")
    else()
        string(TOLOWER ${CMAKE_SYSTEM_NAME} OS)
    endif()

    find_package(libdrm QUIET)

    if (libdrm_FOUND)
        add_compile_options(${LIBDRM_CFLAGS})
        add_compile_options(-DISPDEMO_ENABLE_DRM=1)
    else()
        # use the local drm library in demo dir
        if (TOOLCHAIN_SYSROOT STREQUAL "libc")
            if (ARCH STREQUAL "aarch64")
                set(LOCAL_DRM_LIB_DIR ${CMAKE_SOURCE_DIR}/sample/deps/lib/aarch64/)
            elseif (ARCH STREQUAL "arm")
                set(LOCAL_DRM_LIB_DIR ${CMAKE_SOURCE_DIR}/sample/deps/lib/arm/)
            endif()

            SET(LOCAL_DRM_LIB drm)

            message("\n****** build sample_fec with the local drm library: ${LOCAL_DRM_LIB_DIR}\n")
        endif()
    endif()

    set(ISPFEC_INSTALLED_DIR ${CMAKE_SOURCE_DIR}/build/${OS}/output/${ARCH}/installed/)
    set(ISPFEC_LIB_DIR ${ISPFEC_INSTALLED_DIR}/lib/ ${LOCAL_DRM_LIB})
    set(ISPFEC_LIB IspFec)

    if (NOT libdrm_FOUND)
        set(ISPFEC_INCLUDE_DIR
            ${ISPFEC_INCLUDE_DIR}
            sample/deps/include
            )
    endif()

    set (SAMPLE_FEC_SRCS sample/sample_fec.c)

    include_directories(sample ${ISPFEC_INCLUDE_DIR})
    link_directories("${ISPFEC_LIB_DIR}" "${LOCAL_DRM_LIB_DIR}")

    add_executable(sample_fec ${SAMPLE_FEC_SRCS})
    target_link_libraries(sample_fec ${ISPFEC_LIB} ${LOCAL_DRM_LIB})

    install(TARGETS sample_fec RUNTIME DESTINATION bin)
endif()
