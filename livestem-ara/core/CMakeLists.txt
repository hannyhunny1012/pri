# File watching, WAV loading and buffer handling. No JUCE / ARA dependency so it can be unit tested.
add_library(livestem_core STATIC
    src/RegionPlan.cpp
    src/StemFile.cpp
    src/StemWatcher.cpp
    src/WavFile.cpp)

target_include_directories(livestem_core PUBLIC include)
target_compile_features(livestem_core PUBLIC cxx_std_17)
set_target_properties(livestem_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
target_link_libraries(livestem_core PUBLIC Threads::Threads)
