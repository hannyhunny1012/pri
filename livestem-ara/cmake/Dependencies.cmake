# JUCE and the ARA SDK. Pass -DLIVESTEM_JUCE_DIR=... / -DLIVESTEM_ARA_SDK_DIR=... to use local copies,
# otherwise the pinned versions below are downloaded.
include(FetchContent)

set(LIVESTEM_JUCE_VERSION "9.0.4")
set(LIVESTEM_ARA_SDK_VERSION "releases/2.3.0")

set(LIVESTEM_JUCE_DIR "" CACHE PATH "Local JUCE checkout (optional)")
set(LIVESTEM_ARA_SDK_DIR "" CACHE PATH "Local ARA_SDK checkout with submodules (optional)")

if(LIVESTEM_ARA_SDK_DIR)
    set(ara_sdk_dir "${LIVESTEM_ARA_SDK_DIR}")
else()
    FetchContent_Declare(ara_sdk
        GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
        GIT_TAG ${LIVESTEM_ARA_SDK_VERSION}
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ARA_API ARA_Library
        GIT_PROGRESS TRUE)
    FetchContent_GetProperties(ara_sdk)
    if(NOT ara_sdk_POPULATED)
        FetchContent_Populate(ara_sdk)
    endif()
    set(ara_sdk_dir "${ara_sdk_SOURCE_DIR}")
endif()

if(LIVESTEM_JUCE_DIR)
    add_subdirectory("${LIVESTEM_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${LIVESTEM_JUCE_VERSION}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(juce)
endif()

juce_set_ara_sdk_path("${ara_sdk_dir}")
