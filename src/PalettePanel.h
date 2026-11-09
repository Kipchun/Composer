// Left-hand tool palette: note values, accidentals, articulations and so on.
#pragma once

#include <functional>
#include <vector>

#include "Theme.h"

class PalettePanel : public wxPanel {
public:
    explicit PalettePanel(wxWindow* parent);

    std::function<void(const wxString& tool)> onTool;

private:
    enum class Face { Music, Serif, Plain };
    struct Item {
        wxString label, tip;
        Face face;
        int fontPx;
        wxRect2DDouble rect;
    };
    struct Section {
        wxString title;
        std::vector<Item> items;
        int selected = -1;
        double titleY = 0;
    };

    void Layout2();
    void OnPaint(wxPaintEvent&);
    void OnMotion(wxMouseEvent&);
    void OnLeftDown(wxMouseEvent&);
    bool FindItem(const wxPoint& p, int& section, int& item);

    std::vector<Section> sections_;
    int hoverSection_ = -1, hoverItem_ = -1;
};
