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

  // Bottom action row layout (two rows: edit row, then undo/redo/preview/export row)
  static constexpr int bottomBarRowHeight = 34;
  static constexpr int bottomBarRowGap = 10;
  static constexpr int bottomBarHeight = bottomBarRowHeight * 2 + bottomBarRowGap;
  static constexpr int buttonHeight = 34;
  static constexpr int buttonSpacing = 8;   // between buttons in same group
  static constexpr int groupSpacing = 24;   // between groups
  static constexpr int buttonGap = buttonSpacing;   // alias for compatibility
  static constexpr int groupGap = groupSpacing;
  /** Uniform width for main toolbar buttons. */
  static constexpr int toolbarButtonWidth = 90;
  /** Narrower width for Undo/Redo in bottom row. */
  static constexpr int historyButtonWidth = 56;
  static constexpr int toolbarButtonHeight = 34;
  static constexpr int tertiaryButtonWidth = 56;   // legacy
  static constexpr int secondaryButtonWidth = 72;  // legacy
  static constexpr int primaryButtonWidthExtra = 14;

  // Focus ring (accessibility)
  static constexpr float focusRingThickness = 2.0f;
  static constexpr float focusRingOffset = 2.0f;
};
} // namespace sliceshuffle
