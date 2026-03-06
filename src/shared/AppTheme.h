#pragma once

#include <windows.h>
#include <gdiplus.h>

namespace AppTheme {
inline constexpr COLORREF kBackground = RGB(26, 26, 26);
inline constexpr COLORREF kCardBackground = RGB(45, 45, 45);
inline constexpr COLORREF kCardHover = RGB(68, 68, 68);
inline constexpr COLORREF kCardPressed = RGB(58, 58, 58);
inline constexpr COLORREF kCardBorder = RGB(64, 64, 64);
inline constexpr COLORREF kCardBorderStrong = RGB(85, 85, 85);
inline constexpr COLORREF kCardBorderHover = RGB(100, 100, 100);
inline constexpr COLORREF kTextPrimary = RGB(255, 255, 255);
inline constexpr COLORREF kTextMuted = RGB(230, 230, 230);
inline constexpr COLORREF kTextDisabled = RGB(150, 150, 150);
inline constexpr COLORREF kCheckboxAccent = RGB(135, 170, 220);

inline Gdiplus::Color ToColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}
} // namespace AppTheme
