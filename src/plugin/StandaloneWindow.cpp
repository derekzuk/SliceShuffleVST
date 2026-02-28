#if JucePlugin_Build_Standalone

#include "PluginProcessor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace juce {

class SlicerStandaloneWindow : public StandaloneFilterWindow
{
public:
  using StandaloneFilterWindow::StandaloneFilterWindow;

  void buttonClicked(Button* button) override
  {
    PopupMenu m;
    m.addItem(1, TRANS("Audio/MIDI Settings..."));
    m.addSeparator();
    m.addItem(2, TRANS("Save Settings..."));
    m.addItem(3, TRANS("Load Settings..."));
    m.addSeparator();
    m.addItem(4, TRANS("Reset to default state"));

    m.showMenuAsync(
        PopupMenu::Options().withTargetComponent(button),
        [this](int result)
        {
          if (result == 0)
            return;
          if (result == 2)
          {
            askUserToSaveSettings();
            return;
          }
          if (result == 3)
          {
            askUserToLoadSettings();
            return;
          }
          handleMenuResult(result);
        });
  }

private:
  void askUserToSaveSettings()
  {
    auto* p = dynamic_cast<SlicerPluginProcessor*>(getAudioProcessor());
    if (p == nullptr)
    {
      pluginHolder->askUserToSaveState(".slicerpreset");
      return;
    }
    juce::File suggestedDir = pluginHolder->getLastFile();
    if (!suggestedDir.existsAsFile())
      suggestedDir = suggestedDir.getParentDirectory();
    if (!suggestedDir.isDirectory())
      suggestedDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    juce::File defaultFile = suggestedDir.getChildFile("Preset.slicerpreset");
    auto chooser = std::make_shared<juce::FileChooser>(
        TRANS("Save Settings"), defaultFile, "*.slicerpreset");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, p, chooser](const juce::FileChooser& c)
        {
          if (c.getResults().isEmpty())
            return;
          juce::File f = c.getResult();
          if (f.getFileExtension().isEmpty())
            f = f.withFileExtension("slicerpreset");
          if (p->savePresetToFile(f))
            pluginHolder->setLastFile(c);
        });
  }

  void askUserToLoadSettings()
  {
    auto* p = dynamic_cast<SlicerPluginProcessor*>(getAudioProcessor());
    if (p == nullptr)
    {
      pluginHolder->askUserToLoadState(".slicerpreset");
      return;
    }
    juce::File suggestedDir = pluginHolder->getLastFile();
    if (!suggestedDir.existsAsFile())
      suggestedDir = suggestedDir.getParentDirectory();
    if (!suggestedDir.isDirectory())
      suggestedDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto chooser = std::make_shared<juce::FileChooser>(
        TRANS("Load Settings"), suggestedDir, "*.slicerpreset;*.xml");
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, p, chooser](const juce::FileChooser& c)
        {
          if (c.getResults().isEmpty())
            return;
          juce::File f = c.getResult();
          if (p->loadPresetFromFile(f))
            pluginHolder->setLastFile(c);
        });
  }
};

// Custom app that uses SlicerStandaloneWindow so Options -> Save Settings uses savePresetToFile.
class SlicerStandaloneApp final : public JUCEApplication
{
public:
  SlicerStandaloneApp()
  {
    PropertiesFile::Options options;
    options.applicationName     = CharPointer_UTF8(JucePlugin_Name);
    options.filenameSuffix     = ".settings";
    options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    options.folderName = "~/.config";
#else
    options.folderName = "";
#endif
    appProperties.setStorageParameters(options);
  }

  const String getApplicationName() override { return CharPointer_UTF8(JucePlugin_Name); }
  const String getApplicationVersion() override { return JucePlugin_VersionString; }
  bool moreThanOneInstanceAllowed() override { return true; }
  void anotherInstanceStarted(const String&) override {}

  StandaloneFilterWindow* createWindow()
  {
    if (Desktop::getInstance().getDisplays().displays.isEmpty())
    {
      jassertfalse;
      return nullptr;
    }
    return new SlicerStandaloneWindow(
        getApplicationName(),
        LookAndFeel::getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId),
        createPluginHolder());
  }

  std::unique_ptr<StandalonePluginHolder> createPluginHolder()
  {
    constexpr auto autoOpenMidiDevices =
#if (JUCE_ANDROID || JUCE_IOS) && !JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
        true;
#else
        false;
#endif
#ifdef JucePlugin_PreferredChannelConfigurations
    constexpr StandalonePluginHolder::PluginInOuts channels[]{JucePlugin_PreferredChannelConfigurations};
    const Array<StandalonePluginHolder::PluginInOuts> channelConfig(channels, juce::numElementsInArray(channels));
#else
    const Array<StandalonePluginHolder::PluginInOuts> channelConfig;
#endif
    return std::make_unique<StandalonePluginHolder>(appProperties.getUserSettings(),
                                                    false,
                                                    String{},
                                                    nullptr,
                                                    channelConfig,
                                                    autoOpenMidiDevices);
  }

  void initialise(const String&) override
  {
    mainWindow = rawToUniquePtr(createWindow());
    if (mainWindow != nullptr)
    {
#if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
      Desktop::getInstance().setKioskModeComponent(mainWindow.get(), false);
#endif
      mainWindow->setVisible(true);
    }
    else
    {
      pluginHolder = createPluginHolder();
    }
  }

  void shutdown() override
  {
    pluginHolder = nullptr;
    mainWindow = nullptr;
    appProperties.saveIfNeeded();
  }

  void systemRequestedQuit() override
  {
    if (pluginHolder != nullptr)
      pluginHolder->savePluginState();
    if (mainWindow != nullptr)
      mainWindow->pluginHolder->savePluginState();
    if (ModalComponentManager::getInstance()->cancelAllModalComponents())
    {
      Timer::callAfterDelay(100, []
                           {
                             if (auto app = JUCEApplicationBase::getInstance())
                               app->systemRequestedQuit();
                           });
    }
    else
    {
      quit();
    }
  }

protected:
  ApplicationProperties appProperties;
  std::unique_ptr<StandaloneFilterWindow> mainWindow;

private:
  std::unique_ptr<StandalonePluginHolder> pluginHolder;
};

} // namespace juce

JUCE_CREATE_APPLICATION_DEFINE(juce::SlicerStandaloneApp)

#endif
