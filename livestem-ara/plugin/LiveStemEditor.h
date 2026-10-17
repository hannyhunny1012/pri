#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class LiveStemProcessor;
class LiveStemDocumentController;
class LiveStemAudioSource;

/** Phase 1 UI: pick an audio source, bind it to a WAV, show load status. */
class LiveStemEditor final : public juce::AudioProcessorEditor,
                             public juce::AudioProcessorEditorARAExtension,
                             private juce::ARAEditorView::Listener,
                             private juce::Timer
{
public:
    explicit LiveStemEditor (LiveStemProcessor& processor);
    ~LiveStemEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    juce::AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }

private:
    void timerCallback() override;
    void onNewSelection (const juce::ARAViewSelection& selection) override;

    LiveStemDocumentController* getController() const;
    void refreshSourceList();
    void refreshInfo();
    void selectSource (LiveStemAudioSource* source);
    void chooseFile();

    LiveStemProcessor& processor;

    juce::Label title, sourceLabel, infoLabel;
    juce::ComboBox sourceBox;
    juce::TextButton chooseButton, clearButton;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::vector<LiveStemAudioSource*> listedSources;
    LiveStemAudioSource* selected = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveStemEditor)
};
