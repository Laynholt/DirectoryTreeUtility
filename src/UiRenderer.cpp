#include "UiRenderer.h"

#include <gdiplus.h>

using namespace Gdiplus;

void UiRenderer::DrawCustomButton(HDC hdc, HWND button, const std::wstring& text, bool isPressed, float hoverAlpha) {
    RECT rect;
    GetClientRect(button, &rect);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

    Graphics graphics(memDC);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    graphics.Clear(Color(255, 45, 45, 45));

    Color baseColor = Color(255, 68, 68, 68);
    Color hoverColor = Color(255, 78, 78, 78);
    Color pressedColor = Color(255, 58, 58, 58);

    Color baseBorder = Color(255, 85, 85, 85);
    Color hoverBorder = Color(255, 100, 100, 100);
    Color pressedBorder = Color(255, 80, 80, 80);

    Color baseText = Color(255, 230, 230, 230);
    Color hoverText = Color(255, 255, 255, 255);
    Color pressedText = Color(255, 220, 220, 220);

    Color bgColor;
    Color borderColor;
    Color textColor;
    if (isPressed) {
        bgColor = pressedColor;
        borderColor = pressedBorder;
        textColor = pressedText;
    } else {
        BYTE bgR = static_cast<BYTE>(baseColor.GetRed() + (hoverColor.GetRed() - baseColor.GetRed()) * hoverAlpha);
        BYTE bgG = static_cast<BYTE>(baseColor.GetGreen() + (hoverColor.GetGreen() - baseColor.GetGreen()) * hoverAlpha);
        BYTE bgB = static_cast<BYTE>(baseColor.GetBlue() + (hoverColor.GetBlue() - baseColor.GetBlue()) * hoverAlpha);
        bgColor = Color(255, bgR, bgG, bgB);

        BYTE borderR = static_cast<BYTE>(baseBorder.GetRed() + (hoverBorder.GetRed() - baseBorder.GetRed()) * hoverAlpha);
        BYTE borderG = static_cast<BYTE>(baseBorder.GetGreen() + (hoverBorder.GetGreen() - baseBorder.GetGreen()) * hoverAlpha);
        BYTE borderB = static_cast<BYTE>(baseBorder.GetBlue() + (hoverBorder.GetBlue() - baseBorder.GetBlue()) * hoverAlpha);
        borderColor = Color(255, borderR, borderG, borderB);

        BYTE textR = static_cast<BYTE>(baseText.GetRed() + (hoverText.GetRed() - baseText.GetRed()) * hoverAlpha);
        BYTE textG = static_cast<BYTE>(baseText.GetGreen() + (hoverText.GetGreen() - baseText.GetGreen()) * hoverAlpha);
        BYTE textB = static_cast<BYTE>(baseText.GetBlue() + (hoverText.GetBlue() - baseText.GetBlue()) * hoverAlpha);
        textColor = Color(255, textR, textG, textB);
    }

    SolidBrush bgBrush(bgColor);

    float radius = 4.0f;
    float x = 0.5f;
    float y = 0.5f;
    float width = static_cast<float>(rect.right) - 1.0f;
    float height = static_cast<float>(rect.bottom) - 1.0f;

    GraphicsPath path;
    path.AddArc(x, y, radius * 2.0f, radius * 2.0f, 180.0f, 90.0f);
    path.AddLine(x + radius, y, x + width - radius, y);
    path.AddArc(x + width - radius * 2.0f, y, radius * 2.0f, radius * 2.0f, 270.0f, 90.0f);
    path.AddLine(x + width, y + radius, x + width, y + height - radius);
    path.AddArc(x + width - radius * 2.0f, y + height - radius * 2.0f, radius * 2.0f, radius * 2.0f, 0.0f, 90.0f);
    path.AddLine(x + width - radius, y + height, x + radius, y + height);
    path.AddArc(x, y + height - radius * 2.0f, radius * 2.0f, radius * 2.0f, 90.0f, 90.0f);
    path.AddLine(x, y + height - radius, x, y + radius);
    path.CloseFigure();

    graphics.FillPath(&bgBrush, &path);

    Pen borderPen(borderColor, 1.0f);
    graphics.DrawPath(&borderPen, &path);

    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 12, FontStyleRegular, UnitPoint);
    SolidBrush textBrush(textColor);

    RectF textRect(
        static_cast<REAL>(rect.left),
        static_cast<REAL>(rect.top),
        static_cast<REAL>(rect.right - rect.left),
        static_cast<REAL>(rect.bottom - rect.top)
    );
    StringFormat stringFormat;
    stringFormat.SetAlignment(StringAlignmentCenter);
    stringFormat.SetLineAlignment(StringAlignmentCenter);

    graphics.DrawString(text.c_str(), -1, &font, textRect, &stringFormat, &textBrush);

    BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

void UiRenderer::DrawBackground(HDC hdc, const RECT& rect) {
    HBRUSH bgBrush = CreateSolidBrush(RGB(26, 26, 26));
    FillRect(hdc, &rect, bgBrush);
    DeleteObject(bgBrush);
}

void UiRenderer::DrawCard(HDC hdc, const RECT& rect, const std::wstring& title) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    Color cardBg(255, 45, 45, 45);
    Color borderColor(255, 64, 64, 64);

    GraphicsPath path;
    int radius = 8;
    path.AddArc(rect.left, rect.top, radius * 2, radius * 2, 180, 90);
    path.AddArc(rect.right - radius * 2, rect.top, radius * 2, radius * 2, 270, 90);
    path.AddArc(rect.right - radius * 2, rect.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(rect.left, rect.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    GraphicsPath shadowPath;
    int shadowOffset = 2;
    shadowPath.AddArc(rect.left + shadowOffset, rect.top + shadowOffset, radius * 2, radius * 2, 180, 90);
    shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, rect.top + shadowOffset, radius * 2, radius * 2, 270, 90);
    shadowPath.AddArc(rect.right - radius * 2 + shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 0, 90);
    shadowPath.AddArc(rect.left + shadowOffset, rect.bottom - radius * 2 + shadowOffset, radius * 2, radius * 2, 90, 90);
    shadowPath.CloseFigure();

    SolidBrush shadowBrush(Color(76, 0, 0, 0));
    graphics.FillPath(&shadowBrush, &shadowPath);

    SolidBrush cardBrush(cardBg);
    graphics.FillPath(&cardBrush, &path);

    Pen borderPen(borderColor, 1.0f);
    graphics.DrawPath(&borderPen, &path);

    if (!title.empty()) {
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 12, FontStyleBold, UnitPoint);
        SolidBrush textBrush(Color(255, 255, 255, 255));

        RectF titleRect(
            static_cast<REAL>(rect.left) + 16,
            static_cast<REAL>(rect.top) + 6,
            static_cast<REAL>(rect.right - rect.left - 32),
            24
        );
        StringFormat stringFormat;
        stringFormat.SetAlignment(StringAlignmentNear);
        stringFormat.SetLineAlignment(StringAlignmentCenter);

        graphics.DrawString(title.c_str(), -1, &font, titleRect, &stringFormat, &textBrush);
    }
}

void UiRenderer::DrawEditBorder(HWND parentWindow, HWND editControl) {
    if (!editControl || !parentWindow) {
        return;
    }

    RECT rect;
    GetWindowRect(editControl, &rect);
    ScreenToClient(parentWindow, reinterpret_cast<LPPOINT>(&rect.left));
    ScreenToClient(parentWindow, reinterpret_cast<LPPOINT>(&rect.right));

    HDC hdc = GetDC(parentWindow);
    if (!hdc) {
        return;
    }

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(64, 64, 64));
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));

    MoveToEx(hdc, rect.left - 1, rect.top - 1, nullptr);
    LineTo(hdc, rect.right, rect.top - 1);
    LineTo(hdc, rect.right, rect.bottom);
    LineTo(hdc, rect.left - 1, rect.bottom);
    LineTo(hdc, rect.left - 1, rect.top - 1);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    ReleaseDC(parentWindow, hdc);
}
