// Shared colours, fonts and drawing helpers for the custom-painted UI.
#pragma once

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include <memory>

namespace ui {

inline const wxColour kBg(0x14, 0x16, 0x1B);
inline const wxColour kPanel(0x1D, 0x20, 0x27);
inline const wxColour kPanelAlt(0x28, 0x2C, 0x35);
inline const wxColour kHover(0x34, 0x39, 0x45);
inline const wxColour kBorder(0x38, 0x3D, 0x4A);
inline const wxColour kText(0xE8, 0xEA, 0xF0);
inline const wxColour kMuted(0x9A, 0xA1, 0xB2);
inline const wxColour kAccent(0xE3, 0xB2, 0x4F);   // baton gold
inline const wxColour kAccentInk(0x22, 0x1A, 0x08);
inline const wxColour kRecord(0xE5, 0x5B, 0x5B);
inline const wxColour kPaper(0xFB, 0xF8, 0xF0);
inline const wxColour kInk(0x1D, 0x1D, 0x24);
inline const wxColour kSelect(0x3B, 0x82, 0xF6);
inline const wxColour kProposal(0x12, 0x9B, 0x72);
inline const wxColour kProposalBright(0x4A, 0xD6, 0xA7);

inline wxColour WithAlpha(const wxColour& c, unsigned char a) {
    return wxColour(c.Red(), c.Green(), c.Blue(), a);
}

// Pixel-sized fonts keep the custom layouts independent of the system font size.
inline wxFont UiFont(int px, bool bold = false) {
    wxFontInfo info{wxSize(0, px)};
    info.FaceName("Segoe UI");
    if (bold) info.Bold();
    return wxFont(info);
}
inline wxFont MusicFont(int px) {
    return wxFont(wxFontInfo(wxSize(0, px)).FaceName("Segoe UI Symbol"));
}
inline wxFont SerifFont(int px, bool bold = false, bool italic = false) {
    wxFontInfo info{wxSize(0, px)};
    info.FaceName("Georgia");
    if (bold) info.Bold();
    if (italic) info.Italic();
    return wxFont(info);
}

// Builds a string from a code point; the musical symbols sit outside the BMP.
inline wxString Glyph(unsigned cp) {
    wchar_t buf[3] = {};
    if (cp >= 0x10000) {
        cp -= 0x10000;
        buf[0] = wchar_t(0xD800 + (cp >> 10));
        buf[1] = wchar_t(0xDC00 + (cp & 0x3FF));
    } else {
        buf[0] = wchar_t(cp);
    }
    return wxString(buf);
}

using Gc = std::unique_ptr<wxGraphicsContext>;

inline Gc MakeGc(wxDC& dc) {
    return Gc(wxGraphicsContext::CreateFromUnknownDC(dc));
}

inline void NoPen(wxGraphicsContext& gc) { gc.SetPen(*wxTRANSPARENT_PEN); }

inline void SetStroke(wxGraphicsContext& gc, const wxColour& c, double width) {
    gc.SetPen(gc.CreatePen(wxGraphicsPenInfo(c).Width(width)));
}

inline void FillRound(wxGraphicsContext& gc, const wxRect2DDouble& r, double radius,
                      const wxColour& fill) {
    NoPen(gc);
    gc.SetBrush(wxBrush(fill));
    gc.DrawRoundedRectangle(r.m_x, r.m_y, r.m_width, r.m_height, radius);
}

inline void StrokeRound(wxGraphicsContext& gc, const wxRect2DDouble& r, double radius,
                        const wxColour& c, double width = 1.0) {
    SetStroke(gc, c, width);
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.DrawRoundedRectangle(r.m_x, r.m_y, r.m_width, r.m_height, radius);
}

inline void Line(wxGraphicsContext& gc, double x1, double y1, double x2, double y2,
                 const wxColour& c, double width = 1.0) {
    SetStroke(gc, c, width);
    gc.StrokeLine(x1, y1, x2, y2);
}

inline void DrawTextAt(wxGraphicsContext& gc, const wxString& s, double x, double y,
                       const wxFont& font, const wxColour& c) {
    gc.SetFont(font, c);
    gc.DrawText(s, x, y);
}

// Places text so its baseline is at y, which is what music glyphs need.
inline void DrawBaseline(wxGraphicsContext& gc, const wxString& s, double x, double y,
                         const wxFont& font, const wxColour& c) {
    gc.SetFont(font, c);
    double w, h, descent, leading;
    gc.GetTextExtent(s, &w, &h, &descent, &leading);
    gc.DrawText(s, x, y - (h - descent));
}

inline void DrawCentered(wxGraphicsContext& gc, const wxString& s, const wxRect2DDouble& r,
                         const wxFont& font, const wxColour& c) {
    gc.SetFont(font, c);
    double w, h, descent, leading;
    gc.GetTextExtent(s, &w, &h, &descent, &leading);
    gc.DrawText(s, r.m_x + (r.m_width - w) / 2, r.m_y + (r.m_height - h) / 2);
}

inline double TextWidth(wxGraphicsContext& gc, const wxString& s, const wxFont& font) {
    gc.SetFont(font, *wxBLACK);
    double w, h, descent, leading;
    gc.GetTextExtent(s, &w, &h, &descent, &leading);
    return w;
}

inline bool Hit(const wxRect2DDouble& r, const wxPoint& p) {
    return p.x >= r.m_x && p.x < r.m_x + r.m_width && p.y >= r.m_y && p.y < r.m_y + r.m_height;
}

}  // namespace ui
