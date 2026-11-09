// Left-hand tool palette: note values, accidentals, articulations and so on.
#pragma once

#include <functional>
#include <vector>

#include "Theme.h"

class PalettePanel : public wxPanel {
public:
    explicit PalettePanel(wxWindow* parent);

    struct NoteInput {
        double beats;  // includes the dot
        bool rest;
        char accidental;  // 0 if none
    };

    NoteInput GetNoteInput() const;
    void SetDuration(int index);  // 0 whole ... 4 sixteenth
    void ToggleDot();
    void SetAccidental(char accidental);
    void ClearAccidental();
    void ClearMarks();  // articulation and dynamics buttons

    std::function<void(const wxString& tool)> onTool;
    std::function<void()> onNoteInputChanged;
    // Exactly one is set, or none when a button is turned off. hairpin: 1 crescendo, -1 diminuendo.
    std::function<void(char articulation, const wxString& dynamic, int hairpin)> onMark;

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
    bool IsOn(int section, int item) const;
    void NotifyInput();

    std::vector<Section> sections_;
    int hoverSection_ = -1, hoverItem_ = -1;
    bool dotted_ = false, rest_ = false;
};
