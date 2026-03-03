#pragma once

namespace sliceshuffle
{
/** Design tokens for SliceShuffle UI. Single source of truth for spacing, radii, etc. */
struct UiTokens
{
  // Button appearance
  static constexpr float buttonCornerRadius = 6.0f;
  static constexpr float buttonBorderThickness = 1.0f;
  static constexpr int   buttonPaddingH = 12;
  static constexpr int   buttonPaddingV = 6;
  static constexpr float disabledOpacity = 0.5f;

  // Bottom action row layout
  static constexpr int bottomBarHeight = 28;
  static constexpr int buttonHeight = 24;
  static constexpr int buttonGap = 6;       // intra-group
  static constexpr int groupGap = 12;      // between groups
  static constexpr int tertiaryButtonWidth = 56;   // Undo/Redo
  static constexpr int secondaryButtonWidth = 72;
  static constexpr int primaryButtonWidthExtra = 14; // +width vs secondary (e.g. 86 total)

  // Focus ring (accessibility)
  static constexpr float focusRingThickness = 2.0f;
  static constexpr float focusRingOffset = 2.0f;
};
} // namespace sliceshuffle
