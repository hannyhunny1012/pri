juce_add_plugin(LiveStemARA
    PRODUCT_NAME "LiveStem ARA"
    COMPANY_NAME "LiveStem"
    COMPANY_WEBSITE "https://github.com/hannyhunny1012/pri"
    BUNDLE_ID "com.livestem.ara"
    PLUGIN_MANUFACTURER_CODE Lvsm
    PLUGIN_CODE Lsta
    FORMATS AU VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    IS_ARA_EFFECT TRUE
    ARA_FACTORY_ID "\"com.livestem.ara.factory.0.1.0\""
    ARA_DOCUMENT_ARCHIVE_ID "\"com.livestem.ara.archive.v1\""
    ARA_TRANSFORMATION_FLAGS kARAPlaybackTransformationNoChanges
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(LiveStemARA PRIVATE
    LiveStemAudioSource.cpp
    LiveStemDocumentController.cpp
    LiveStemEditor.cpp
    LiveStemPlaybackRenderer.cpp
    LiveStemProcessor.cpp)

target_compile_definitions(LiveStemARA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(LiveStemARA
    PRIVATE
        livestem_core
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
