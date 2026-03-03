#pragma once

#include <JuceHeader.h>
#include "UiTokens.h"

namespace cutshuffle
{
/** Button variant: determines visual style. Set via button.getProperties().set("variant", "primary"). */
enum class ButtonVariant
{
  Primary,    // main call to action
  Secondary,  // normal action
  Tertiary,   // low emphasis / toolbar
  Destructive // Reset, Delete, etc.
};

/** LookAndFeel that draws TextButtons by variant (primary/secondary/tertiary/destructive).
 *  Reads variant from Button::getProperties() ["variant" -> "primary"|"secondary"|"tertiary"|"destructive"].
 *  Handles Normal, Hover, Down, Disabled, and Toggled (for buttons with setClickingTogglesState).
 */
class CutShuffleLookAndFeel : public juce::LookAndFeel_V4
{
public:
  CutShuffleLookAndFeel();
  ~CutShuffleLookAndFeel() override = default;

  juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
  int getTextButtonWidthToFitText (juce::TextButton&, int buttonHeight) override;
  void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                             bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
  void drawButtonText (juce::Graphics&, juce::TextButton&,
                       bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

  /** Resolve variant from button properties; default secondary. */
  static ButtonVariant getButtonVariant (const juce::Button& button);

private:
  struct ButtonColors
  {
    juce::Colour fill;
    juce::Colour border;
    juce::Colour text;
  };
  ButtonColors getColorsForVariant (ButtonVariant v, bool enabled, bool highlighted, bool down,
                                    bool toggledOn);
  void drawFocusRing (juce::Graphics& g, juce::Button& button, juce::Rectangle<float> bounds);
};
} // namespace cutshuffle
