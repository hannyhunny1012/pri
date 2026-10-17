#include "LiveStemEditor.h"

#include "LiveStemDocumentController.h"
#include "LiveStemProcessor.h"

namespace
{
juce::String jp (const char* utf8) { return juce::String::fromUTF8 (utf8); }

juce::String formatDuration (int64_t frames, double rate)
{
    if (rate <= 0.0)
        return "-";

    const auto seconds = (double) frames / rate;
    const auto minutes = (int) (seconds / 60.0);
    return juce::String (minutes) + ":" + juce::String (seconds - minutes * 60.0, 3).paddedLeft ('0', 6);
}
} // namespace

LiveStemEditor::LiveStemEditor (LiveStemProcessor& p)
    : juce::AudioProcessorEditor (&p),
      juce::AudioProcessorEditorARAExtension (&p),
      processor (p)
{
    title.setText (jp ("LiveStem ARA（フェーズ1 試作）"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    addAndMakeVisible (title);

    sourceLabel.setText (jp ("対象の音声ソース"), juce::dontSendNotification);
    addAndMakeVisible (sourceLabel);

    sourceBox.onChange = [this]
    {
        const auto index = sourceBox.getSelectedItemIndex();
        selected = juce::isPositiveAndBelow (index, (int) listedSources.size()) ? listedSources[(size_t) index] : nullptr;
        refreshInfo();
    };
    addAndMakeVisible (sourceBox);

    chooseButton.setButtonText (jp ("WAVを選択..."));
    chooseButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (chooseButton);

    clearButton.setButtonText (jp ("対応付けを解除"));
    clearButton.onClick = [this]
    {
        if (auto* c = getController(); c != nullptr && selected != nullptr)
            c->setStemPath (*selected, {});

        refreshInfo();
    };
    addAndMakeVisible (clearButton);

    infoLabel.setJustificationType (juce::Justification::topLeft);
    infoLabel.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (infoLabel);

    if (auto* view = getARAEditorView())
        view->addListener (this);

    refreshSourceList();

    // Default to the source this plug-in instance plays.
    if (auto* renderer = processor.getPlaybackRenderer())
        for (auto* region : renderer->getPlaybackRegions())
        {
            selectSource (region->getAudioModification()->getAudioSource<LiveStemAudioSource>());
            break;
        }

    refreshInfo();
    setResizable (true, false);
    setSize (560, 300);
    startTimer (250);
}

LiveStemEditor::~LiveStemEditor()
{
    if (auto* view = getARAEditorView())
        view->removeListener (this);
}

LiveStemDocumentController* LiveStemEditor::getController() const
{
    if (auto* view = getARAEditorView())
        return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<LiveStemDocumentController> (view->getDocumentController());

    return nullptr;
}

void LiveStemEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void LiveStemEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    title.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);

    auto row = area.removeFromTop (26);
    sourceLabel.setBounds (row.removeFromLeft (120));
    sourceBox.setBounds (row);
    area.removeFromTop (8);

    row = area.removeFromTop (28);
    chooseButton.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (8);
    clearButton.setBounds (row.removeFromLeft (140));
    area.removeFromTop (8);

    infoLabel.setBounds (area);
}

void LiveStemEditor::timerCallback()
{
    refreshSourceList();
    refreshInfo();
}

void LiveStemEditor::onNewSelection (const juce::ARAViewSelection& selection)
{
    const auto& regions = selection.getPlaybackRegions<juce::ARAPlaybackRegion>();

    if (! regions.empty())
        selectSource (regions.front()->getAudioModification()->getAudioSource<LiveStemAudioSource>());
}

void LiveStemEditor::refreshSourceList()
{
    auto* controller = getController();
    const auto sources = controller != nullptr ? controller->getAudioSources() : std::vector<LiveStemAudioSource*>();

    if (sources == listedSources)
        return;

    listedSources = sources;

    if (std::find (listedSources.begin(), listedSources.end(), selected) == listedSources.end())
        selected = listedSources.empty() ? nullptr : listedSources.front();

    sourceBox.clear (juce::dontSendNotification);

    for (size_t i = 0; i < listedSources.size(); ++i)
        sourceBox.addItem (listedSources[i]->getDisplayName(), (int) i + 1);

    selectSource (selected);
}

void LiveStemEditor::selectSource (LiveStemAudioSource* source)
{
    const auto it = std::find (listedSources.begin(), listedSources.end(), source);

    if (it == listedSources.end())
        return;

    selected = source;
    sourceBox.setSelectedItemIndex ((int) std::distance (listedSources.begin(), it), juce::dontSendNotification);
    refreshInfo();
}

void LiveStemEditor::refreshInfo()
{
    auto* controller = getController();

    // The host may have removed the source since the last list refresh.
    if (controller != nullptr && selected != nullptr)
    {
        const auto current = controller->getAudioSources();

        if (std::find (current.begin(), current.end(), selected) == current.end())
            selected = nullptr;
    }

    const bool usable = controller != nullptr && selected != nullptr;
    chooseButton.setEnabled (usable);
    clearButton.setEnabled (usable);

    if (controller == nullptr)
    {
        infoLabel.setText (jp ("ARAホストが検出されません。Logic Pro / Studio One でクリップにARAプラグインとして適用してください。"), juce::dontSendNotification);
        return;
    }

    if (selected == nullptr)
    {
        infoLabel.setText (jp ("音声ソースがありません。"), juce::dontSendNotification);
        return;
    }

    const auto info = selected->getStem().getInfo();
    juce::StringArray lines;

    lines.add (jp ("ファイル: ") + (info.path.empty() ? jp ("（未指定）") : fromStdPath (info.path)));
    lines.add (jp ("状態: ") + jp (livestem::statusName (info.status)));

    if (info.loadedAtUnixMs != 0)
    {
        lines.add (jp ("最終読み込み: ") + juce::Time (info.loadedAtUnixMs).formatted ("%Y-%m-%d %H:%M:%S"));
        lines.add (jp ("内容: ") + juce::String (info.sampleRate, 0) + " Hz / " + juce::String (info.numChannels) + "ch / "
                   + formatDuration (info.numFrames, info.sampleRate));
    }

    const auto sourceSeconds = (double) selected->getSampleCount() / selected->getSampleRate();
    lines.add (jp ("元のクリップ: ") + juce::String (selected->getSampleRate(), 0) + " Hz / "
               + juce::String (selected->getChannelCount()) + "ch / " + formatDuration ((int64_t) (sourceSeconds * 1000.0), 1000.0));

    if (! info.message.empty())
        lines.add (jp ("メッセージ: ") + jp (info.message.c_str()));

    const auto renderRate = controller->getRenderSampleRate();

    if (info.sampleRate > 0.0 && renderRate > 0.0 && ! juce::exactlyEqual (info.sampleRate, renderRate))
        lines.add (jp ("警告: ファイルのサンプルレート (") + juce::String (info.sampleRate, 0) + jp (" Hz) がプロジェクト (")
                   + juce::String (renderRate, 0) + jp (" Hz) と違うため、元の音声を再生しています。"));

    if (info.numFrames > 0 && info.sampleRate > 0.0 && std::abs ((double) info.numFrames / info.sampleRate - sourceSeconds) > 0.001)
        lines.add (jp ("注意: ファイルの長さが元のクリップと違います（短い分は無音、長い分は使われません）。"));

    infoLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

void LiveStemEditor::chooseFile()
{
    if (selected == nullptr)
        return;

    const auto current = selected->getStem().getPath();
    const auto startDir = current.empty() ? juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                          : juce::File (fromStdPath (current)).getParentDirectory();

    fileChooser = std::make_unique<juce::FileChooser> (jp ("共有WAVを選択"), startDir, "*.wav");

    juce::Component::SafePointer<LiveStemEditor> safeThis (this);
    auto* target = selected;

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis, target] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr || chooser.getResult() == juce::File())
                                      return;

                                  auto* c = safeThis->getController();

                                  if (c == nullptr)
                                      return;

                                  // The source may have been removed while the dialog was open.
                                  const auto sources = c->getAudioSources();

                                  if (std::find (sources.begin(), sources.end(), target) != sources.end())
                                      c->setStemPath (*target, chooser.getResult());

                                  safeThis->refreshInfo();
                              });
}
