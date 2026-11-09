// The New Project and Project Settings dialogs.
#pragma once

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/statline.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>

#include <functional>
#include <map>
#include <vector>

#include "Project.h"
#include "audio/Vst3Host.h"

// The form shared by both dialogs.
class SettingsForm : public wxPanel {
public:
    SettingsForm(wxWindow* parent, const ProjectSettings& initial);
    ProjectSettings Get() const;

private:
    wxTextCtrl* title_;
    wxTextCtrl* subtitle_;
    wxTextCtrl* composer_;
    wxTextCtrl* tempoText_;
    wxSpinCtrl* tempo_;
    wxChoice* key_;
    wxSpinCtrl* timeTop_;
    wxChoice* timeBottom_;
    wxSpinCtrl* bars_;
    wxSpinCtrl* perLine_;
};

class ProjectSettingsDialog : public wxDialog {
public:
    ProjectSettingsDialog(wxWindow* parent, const ProjectSettings& current);
    ProjectSettings Get() const { return form_->Get(); }

private:
    SettingsForm* form_;
};

class MeterDialog : public wxDialog {
public:
    MeterDialog(wxWindow* parent, int bar, int top, int bottom);
    int Top() const { return top_->GetValue(); }
    int Bottom() const { return wxAtoi(bottom_->GetStringSelection()); }

private:
    wxSpinCtrl* top_;
    wxChoice* bottom_;
};

class TempoDialog : public wxDialog {
public:
    TempoDialog(wxWindow* parent, int bar, int bpm, const wxString& text, bool hasChange);
    int Bpm() const { return bpm_->GetValue(); }
    wxString Text() const { return text_->GetValue(); }
    bool RemoveRequested() const { return remove_; }

private:
    wxSpinCtrl* bpm_;
    wxTextCtrl* text_;
    bool remove_ = false;
};

// Two lists: every available instrument, and the score's parts in orchestral order.
class InstrumentPicker : public wxPanel {
public:
    explicit InstrumentPicker(wxWindow* parent);
    void SetParts(std::vector<ScorePart> parts);
    const std::vector<ScorePart>& GetParts() const { return parts_; }
    std::function<void()> onChanged;

private:
    void AddSelected();
    void RemoveSelected();
    void RefreshChosen();

    wxListBox* available_;
    wxListBox* chosen_;
    std::vector<int> availableMap_;  // list row -> catalog index, -1 for a heading
    std::vector<ScorePart> parts_;
};

class ChangeInstrumentsDialog : public wxDialog {
public:
    ChangeInstrumentsDialog(wxWindow* parent, const std::vector<ScorePart>& current);
    const std::vector<ScorePart>& GetParts() const { return picker_->GetParts(); }

private:
    InstrumentPicker* picker_;
};

class NewProjectDialog : public wxDialog {
public:
    explicit NewProjectDialog(wxWindow* parent);
    Project BuildResult() const;

private:
    enum { kFull, kSmall, kSoloist, kCustom };

    void UpdatePage();
    void ShowStep(bool sounds);
    void BuildSoundRows();
    std::vector<int> Instruments() const;

    wxBoxSizer* sizer_ = nullptr;
    wxBoxSizer* topSizer_ = nullptr;
    wxStaticLine* divider_ = nullptr;
    wxListBox* templates_;
    wxStaticText* heading_;
    wxListBox* preview_;
    wxChoice* soloist_;
    InstrumentPicker* picker_;
    SettingsForm* form_;

    wxPanel* soundsPage_ = nullptr;
    wxChoice* setAll_ = nullptr;
    wxScrolledWindow* soundRows_ = nullptr;
    std::vector<wxChoice*> soundChoices_;
    std::vector<PluginInfo> plugins_;
    std::map<wxString, int> rememberedSounds_;  // kept when going back a step

    wxButton* back_ = nullptr;
    wxButton* next_ = nullptr;
    wxButton* create_ = nullptr;
};
