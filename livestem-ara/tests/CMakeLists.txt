include(FetchContent)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(doctest)
if(NOT doctest_POPULATED)
    FetchContent_Populate(doctest)
endif()

add_executable(livestem_tests
    main.cpp
    EditGuardTests.cpp
    RegionPlanTests.cpp
    SharedSlotTests.cpp
    StabilityTrackerTests.cpp
    StemFileTests.cpp
    WavFileTests.cpp)

target_include_directories(livestem_tests PRIVATE "${doctest_SOURCE_DIR}")
target_link_libraries(livestem_tests PRIVATE livestem_core)
add_test(NAME livestem_tests COMMAND livestem_tests)

# Rewrites a WAV continuously while a simulated audio thread plays from it.
# Usage: livestem_stress [seconds] [rewriteIntervalMs]
add_executable(livestem_stress StressTest.cpp)
target_link_libraries(livestem_stress PRIVATE livestem_core)
add_test(NAME livestem_stress_short COMMAND livestem_stress 5 100)
