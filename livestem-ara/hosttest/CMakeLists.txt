# Minimal ARA host that loads the built VST3 plug-in in ARA mode and checks
# file swapping, region edits, offline rendering and archive store/restore without a DAW.
juce_add_console_app(livestem_ara_hosttest PRODUCT_NAME "livestem_ara_hosttest")

target_sources(livestem_ara_hosttest PRIVATE AraHostTest.cpp)

target_compile_definitions(livestem_ara_hosttest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_ARA=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(livestem_ara_hosttest PRIVATE
    livestem_core
    juce::juce_audio_processors_headless
    juce::juce_audio_formats
    juce::juce_events
    juce::juce_recommended_config_flags)
