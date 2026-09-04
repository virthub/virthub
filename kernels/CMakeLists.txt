# virthub/kernels/CMakeLists.txt

cmake_minimum_required(VERSION 3.18)

# Option to enable GPU kernel builds.
# Default is OFF because the current CUDA 12.0 + glibc combination
# fails to compile C++17 host code due to _Float32/_Float64/_Float128
# declarations. Set this to ON after upgrading to CUDA 12.3+ or using
# a compatible host compiler.
option(ENABLE_GPU_KERNELS "Build PSP-KV GPU kernels" OFF)

# Always use C++ as the base language; CUDA is added only if needed.
project(psp_kv_kernels LANGUAGES CXX)

if(ENABLE_GPU_KERNELS)
    # ------------------------------------------------------------------
    # Enable CUDA and set standards
    # ------------------------------------------------------------------
    enable_language(CUDA)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)   # Use -std=c++17, not gnu++17

    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_EXTENSIONS OFF)

    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR CMAKE_CUDA_ARCHITECTURES STREQUAL "")
        set(CMAKE_CUDA_ARCHITECTURES "80;86;89;90" CACHE STRING "CUDA architectures to target" FORCE)
        message(STATUS "Defaulting CUDA architectures to: ${CMAKE_CUDA_ARCHITECTURES}")
    endif()

    # Include directories
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/basic_format
        ${CMAKE_CURRENT_SOURCE_DIR}/enhanced_format
        ${CMAKE_CURRENT_SOURCE_DIR}/btkv_format
    )

    # Explicit list of source files (tma_descriptors.cu excluded for now)
    set(PSP_KV_KERNEL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/basic_format/dequant_kernel.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/basic_format/paged_attention_basic.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/enhanced_format/dequant_kernel.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/enhanced_format/paged_attention_enhanced.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/btkv_format/dequant_kernel.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/btkv_format/paged_attention_btkv.cu
    )

    add_library(psp_kv_kernels SHARED ${PSP_KV_KERNEL_SOURCES})
    target_include_directories(psp_kv_kernels PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/basic_format
        ${CMAKE_CURRENT_SOURCE_DIR}/enhanced_format
        ${CMAKE_CURRENT_SOURCE_DIR}/btkv_format
    )

    # CPU reference test
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpu_reference_dequant.cu)
        add_executable(cpu_reference_dequant
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpu_reference_dequant.cu
        )
        target_include_directories(cpu_reference_dequant PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/common
            ${CMAKE_CURRENT_SOURCE_DIR}/basic_format
            ${CMAKE_CURRENT_SOURCE_DIR}/enhanced_format
            ${CMAKE_CURRENT_SOURCE_DIR}/btkv_format
        )
    endif()

    message(STATUS "GPU kernel build enabled. If compilation fails due to _Float32 errors, upgrade CUDA to 12.3+ or use a compatible host compiler.")
else()
    message(STATUS "GPU kernel build disabled (default). Use -DENABLE_GPU_KERNELS=ON to build after upgrading CUDA/glibc.")
    # No targets are created; the project is effectively a no-op for now.
endif()
