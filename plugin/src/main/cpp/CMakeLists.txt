cmake_minimum_required(VERSION 3.22.1)

project(filmsim_ufx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/spektrafilm)
set(ENGINE_SHADER_DIR ${ENGINE_DIR}/shaders)
set(ENGINE_DATA_DIR ${ENGINE_DIR}/Resources/data)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(SHADER_TOOLS_DIR "${ANDROID_NDK}/shader-tools/windows-x86_64")
    set(EXE_SUFFIX ".exe")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(SHADER_TOOLS_DIR "${ANDROID_NDK}/shader-tools/darwin-x86_64")
    set(EXE_SUFFIX "")
else()
    set(SHADER_TOOLS_DIR "${ANDROID_NDK}/shader-tools/linux-x86_64")
    set(EXE_SUFFIX "")
endif()
set(GLSLC "${SHADER_TOOLS_DIR}/glslc${EXE_SUFFIX}")
set(SPIRV_VAL "${SHADER_TOOLS_DIR}/spirv-val${EXE_SUFFIX}")
foreach(TOOL ${GLSLC} ${SPIRV_VAL})
    if(NOT EXISTS "${TOOL}")
        message(FATAL_ERROR "NDK shader tool not found: ${TOOL}")
    endif()
endforeach()

# Profile tables and spectral data, generated from the JSON profiles.
set(PROFILE_CURVES_CPP ${GENERATED_DIR}/SpektraGeneratedProfileCurves.cpp)
set(PROFILE_COUNTS_H ${GENERATED_DIR}/SpektraGeneratedProfileCounts.h)
set(HANATOS_F32 ${GENERATED_DIR}/SpektraHanatos2025Spectra.f32)
set(GAMUT_F32 ${GENERATED_DIR}/SpektraOutputGamutCompression.f32)
set(HANATOS_H ${GENERATED_DIR}/SpektraHanatos2025Spectra_f32.h)
set(GAMUT_H ${GENERATED_DIR}/SpektraOutputGamutCompression_f32.h)

file(GLOB PROFILE_JSON CONFIGURE_DEPENDS
    "${ENGINE_DATA_DIR}/profiles/*.json"
    "${ENGINE_DATA_DIR}/profiles/archive/*.json")
file(GLOB_RECURSE RESOURCE_DATA CONFIGURE_DEPENDS
    "${ENGINE_DATA_DIR}/filters/*"
    "${ENGINE_DATA_DIR}/luts/*")

add_custom_command(
    OUTPUT ${PROFILE_CURVES_CPP} ${PROFILE_COUNTS_H} ${HANATOS_F32} ${GAMUT_F32}
    COMMAND ${CMAKE_COMMAND} -E env "SPEKTRAFILM_DATA_DIR=${ENGINE_DATA_DIR}"
        "${Python3_EXECUTABLE}" "${ENGINE_DIR}/tools/generate_profile_curves.py"
        --output "${PROFILE_CURVES_CPP}"
        --counts-output "${PROFILE_COUNTS_H}"
        --hanatos-output "${HANATOS_F32}"
        --output-gamut-compression-output "${GAMUT_F32}"
    DEPENDS
        "${ENGINE_DIR}/tools/generate_profile_curves.py"
        "${ENGINE_DIR}/tools/ofx_stock_lists.py"
        ${PROFILE_JSON} ${RESOURCE_DATA}
    COMMENT "Generating film profile tables"
    VERBATIM)

foreach(PAIR "${HANATOS_F32}|${HANATOS_H}|SpektraHanatos2025Spectra_f32"
             "${GAMUT_F32}|${GAMUT_H}|SpektraOutputGamutCompression_f32")
    string(REPLACE "|" ";" PARTS "${PAIR}")
    list(GET PARTS 0 IN)
    list(GET PARTS 1 OUT)
    list(GET PARTS 2 NAME)
    add_custom_command(
        OUTPUT ${OUT}
        COMMAND "${Python3_EXECUTABLE}" "${ENGINE_DIR}/tools/bin_to_header.py"
            --input "${IN}" --output "${OUT}" --var-name "${NAME}"
        DEPENDS ${IN} "${ENGINE_DIR}/tools/bin_to_header.py"
        COMMENT "Embedding ${NAME}"
        VERBATIM)
    list(APPEND EMBEDDED_HEADERS ${OUT})
endforeach()

# Compute shaders, compiled to SPIR-V and embedded in the library.
# Each entry: output name | source file | extra glslc options.
set(SHADERS
    "SpektraCopy|SpektraCopy.comp|-O"
    "SpektraFormatConvert|SpektraFormatConvert.comp|-O"
    "SpektraImageFormatConvert|SpektraImageFormatConvert.comp|-O"
    "SpektraAutoExposure|SpektraAutoExposure.comp|-O"
    "SpektraAutoExposureImage|SpektraAutoExposure.comp|-O,-DSPEKTRA_AUTO_EXPOSURE_IMAGE=1"
    "SpektraFilmExposure|SpektraFilmExposure.comp|-O"
    "SpektraCurveDevelop|SpektraCurveDevelop.comp|-O"
    # The large print and scan programs are built unoptimised: older Adreno
    # drivers fail pipeline creation on the optimised SPIR-V.
    "SpektraPrintScan|SpektraPrintScan.comp|-O0"
    "SpektraPrintNegative|SpektraPrintScan.comp|-O0,-DSPEKTRA_NEGATIVE_SETUP=1"
    "SpektraPrintScanVideo|SpektraPrintScan.comp|-O0,-DSPEKTRA_VIDEO_BANDS=1"
    "SpektraPrintNegativeVideo|SpektraPrintScan.comp|-O0,-DSPEKTRA_NEGATIVE_SETUP=1,-DSPEKTRA_VIDEO_BANDS=1"
    "SpektraPrintFourStageBuild|SpektraPrintScan.comp|-O0,-DSPEKTRA_FOUR_STAGE_LUT_BUILD=1,-DSPEKTRA_VIDEO_BANDS=1"
    "SpektraHalation|SpektraHalation.comp|-O"
    "SpektraDiffusion|SpektraDiffusion.comp|-O"
    "SpektraDir|SpektraDir.comp|-O"
    "SpektraScannerPost|SpektraScannerPost.comp|-O"
    "SpektraGrain|SpektraGrain.comp|-O"
    "SpektraGrainBlur|SpektraGrainBlur.comp|-O"
    "SpektraVideoEffects|SpektraVideoEffects.comp|-O"
    "SpektraVideoLut|SpektraVideoLut.comp|-O"
    # Same program for GPUs that cannot filter 32-bit float textures; blends table texels in the shader.
    "SpektraVideoLutTexelFetch|SpektraVideoLut.comp|-O,-DSPEKTRA_FOUR_STAGE_LUT_TEXEL_FETCH=1"
)
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS "${ENGINE_SHADER_DIR}/*.glsl" "${ENGINE_SHADER_DIR}/*.h")
foreach(ENTRY ${SHADERS})
    string(REPLACE "|" ";" PARTS "${ENTRY}")
    list(GET PARTS 0 NAME)
    list(GET PARTS 1 SOURCE)
    list(GET PARTS 2 OPTIONS)
    string(REPLACE "," ";" OPTIONS "${OPTIONS}")
    set(SPV ${GENERATED_DIR}/${NAME}.spv)
    set(HEADER ${GENERATED_DIR}/${NAME}_spv.h)
    add_custom_command(
        OUTPUT ${HEADER}
        BYPRODUCTS ${SPV}
        COMMAND "${GLSLC}" --target-env=vulkan1.1 -fshader-stage=compute ${OPTIONS}
            -I "${ENGINE_SHADER_DIR}" "${ENGINE_SHADER_DIR}/${SOURCE}" -o "${SPV}"
        COMMAND "${SPIRV_VAL}" --target-env vulkan1.1 "${SPV}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/spirv_to_header.py"
            --input "${SPV}" --output "${HEADER}" --name "${NAME}_spv"
        DEPENDS "${ENGINE_SHADER_DIR}/${SOURCE}" ${SHADER_INCLUDES}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/spirv_to_header.py"
        COMMENT "Compiling shader ${NAME}"
        VERBATIM)
    list(APPEND EMBEDDED_HEADERS ${HEADER})
endforeach()

add_custom_target(embedded_resources DEPENDS ${EMBEDDED_HEADERS} ${PROFILE_COUNTS_H})

add_library(filmsim_ufx SHARED
    plugin/FilmPlugin.cpp
    spektrafilm/src/SpektraVulkanRenderer.cpp
    ${PROFILE_CURVES_CPP}
)
add_dependencies(filmsim_ufx embedded_resources)

target_include_directories(filmsim_ufx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ufx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    ${ENGINE_DIR}/src
    ${GENERATED_DIR}
)

target_compile_options(filmsim_ufx PRIVATE -O3 -fvisibility=hidden -fvisibility-inlines-hidden)

# Missing spectral measurements must be rejected before upload, which needs IEEE NaN checks.
set_source_files_properties(spektrafilm/src/SpektraVulkanRenderer.cpp ${PROFILE_CURVES_CPP}
    PROPERTIES COMPILE_OPTIONS "-fno-fast-math")

find_library(log-lib log)
find_library(vulkan-lib vulkan)
target_link_libraries(filmsim_ufx PRIVATE ${vulkan-lib} ${log-lib})

# Only the plug-in entry points are exported.
target_link_options(filmsim_ufx PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL)
