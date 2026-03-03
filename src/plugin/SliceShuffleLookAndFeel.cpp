#include "SliceShuffleLookAndFeel.h"

namespace sliceshuffle
{
static juce::Identifier variantKey ("variant");

SliceShuffleLookAndFeel::SliceShuffleLookAndFeel()
  : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::getDarkColourScheme())
{
}

ButtonVariant SliceShuffleLookAndFeel::getButtonVariant (const juce::Button& button)
{
  const juce::var v = button.getProperties().getWithDefault (variantKey, juce::var ("secondary"));
  const juce::String s = v.toString().toLowerCase();
  if (s == "primary")    return ButtonVariant::Primary;
  if (s == "tertiary")   return ButtonVariant::Tertiary;
  if (s == "destructive") return ButtonVariant::Destructive;
  return ButtonVariant::Secondary;
}

SliceShuffleLookAndFeel::ButtonColors
SliceShuffleLookAndFeel::getColorsForVariant (ButtonVariant v, bool enabled, bool highlighted,
                                            bool down, bool toggledOn)
{
  auto& scheme = getCurrentColourScheme();
  ButtonColors c;
  const float hoverDelta = 0.08f;
  const float downDelta = -0.06f;

  switch (v)
  {
  case ButtonVariant::Primary:
    c.fill   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill);
    c.border = c.fill.brighter (0.15f);
    c.text   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedText);
    break;
  case ButtonVariant::Secondary:
    c.fill   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground);
    c.border = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::outline);
    c.text   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText);
    break;
  case ButtonVariant::Tertiary:
    c.fill   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground).darker (0.15f);
    c.border = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::outline).withAlpha (0.6f);
    c.text   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText).withAlpha (0.9f);
    break;
  case ButtonVariant::Destructive:
    c.fill   = juce::Colour (0xff8b2635);
    c.border = juce::Colour (0xffa03040);
    c.text   = juce::Colours::white;
    break;
  }

  if (toggledOn && v != ButtonVariant::Destructive)
  {
    c.fill   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill).withAlpha (0.7f);
    c.border = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill);
    c.text   = scheme.getUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedText);
  }

  if (enabled)
  {
    if (down)
      c.fill = c.fill.darker (1.0f - downDelta);
    else if (highlighted)
      c.fill = c.fill.brighter (1.0f + hoverDelta);
  }
  else
  {
    c.fill   = c.fill.withAlpha (UiTokens::disabledOpacity);
    c.border = c.border.withAlpha (UiTokens::disabledOpacity);
    c.text   = c.text.withAlpha (UiTokens::disabledOpacity);
  }

  return c;
}

void SliceShuffleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& /*backgroundColour*/,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
  const bool enabled = button.isEnabled();
  const bool toggled = button.getToggleState();
  const ButtonVariant variant = getButtonVariant (button);
  const ButtonColors c = getColorsForVariant (variant, enabled, shouldDrawButtonAsHighlighted,
                                              shouldDrawButtonAsDown, toggled);

  auto bounds = button.getLocalBounds().toFloat().reduced (0.5f, 0.5f);

  g.setColour (c.fill);
  g.fillRoundedRectangle (bounds, UiTokens::buttonCornerRadius);

  g.setColour (c.border);
  g.drawRoundedRectangle (bounds, UiTokens::buttonCornerRadius, UiTokens::buttonBorderThickness);

  if (button.hasKeyboardFocus (true))
    drawFocusRing (g, button, bounds);
}

void SliceShuffleLookAndFeel::drawFocusRing (juce::Graphics& g, juce::Button&,
                                          juce::Rectangle<float> bounds)
{
  auto& scheme = getCurrentColourScheme();
  juce::Colour ringColour = scheme.getUIColour (
      juce::LookAndFeel_V4::ColourScheme::defaultFill);
  auto ringBounds = bounds.expanded (UiTokens::focusRingOffset);
  g.setColour (ringColour.withAlpha (0.6f));
  g.drawRoundedRectangle (ringBounds, UiTokens::buttonCornerRadius + UiTokens::focusRingOffset,
                           UiTokens::focusRingThickness);
}

void SliceShuffleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
  const bool enabled = button.isEnabled();
  const bool toggled = button.getToggleState();
  const ButtonVariant variant = getButtonVariant (button);
  const ButtonColors c = getColorsForVariant (variant, enabled, shouldDrawButtonAsHighlighted,
                                              shouldDrawButtonAsDown, toggled);

  if (!enabled)
    g.setOpacity (UiTokens::disabledOpacity);

  g.setColour (c.text);
  g.setFont (getTextButtonFont (button, button.getHeight()));
  g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (UiTokens::buttonPaddingH,
                                                                              UiTokens::buttonPaddingV),
                    juce::Justification::centred, 2);
}

juce::Font SliceShuffleLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
  const float size = juce::jmin (14.0f, (float) buttonHeight * 0.55f);
  return withDefaultMetrics (juce::FontOptions (size));
}

int SliceShuffleLookAndFeel::getTextButtonWidthToFitText (juce::TextButton& button, int buttonHeight)
{
  const float textW = juce::TextLayout::getStringWidth (getTextButtonFont (button, buttonHeight),
                                                        button.getButtonText());
  return juce::roundToInt (textW) + UiTokens::buttonPaddingH * 2;
}
} // namespace sliceshuffle
