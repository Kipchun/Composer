#include "ProjectDialogs.h"

#include <wx/sizer.h>
#include <wx/statline.h>

#include <algorithm>

#include "Theme.h"

using namespace ui;

namespace {

wxStaticText* Label(wxWindow* parent, const wxString& text, const wxColour& color = kMuted) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(color);
    return label;
}

// Key choices run from 7 flats to 7 sharps, majors first.
int KeyIndex(int fifths, bool minor) { return fifths + 7 + (minor ? 15 : 0); }

ProjectSettings NewProjectDefaults() {
    ProjectSettings s;
    s.title = "Untitled";
    s.tempoText = "Moderato";
    return s;
}

}  // namespace

SettingsForm::SettingsForm(wxWindow* parent, const ProjectSettings& s) : wxPanel(parent) {
    SetBackgroundColour(kPanel);

    title_ = new wxTextCtrl(this, wxID_ANY, s.title);
    subtitle_ = new wxTextCtrl(this, wxID_ANY, s.subtitle);
    composer_ = new wxTextCtrl(this, wxID_ANY, s.composer);
    tempoText_ = new wxTextCtrl(this, wxID_ANY, s.tempoText);
    tempo_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 20, 300, s.tempo);

    wxArrayString keys;
    for (int minor = 0; minor < 2; ++minor)
        for (int fifths = -7; fifths <= 7; ++fifths) keys.Add(KeyLabel(fifths, minor != 0));
    key_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(130, -1), keys);
    key_->SetSelection(KeyIndex(s.fifths, s.minor));

    timeTop_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(64, -1), wxSP_ARROW_KEYS, 1, 16, s.timeTop);
    timeBottom_ = new wxChoice(this, wxID_ANY);
    for (const char* value : {"2", "4", "8"}) timeBottom_->Append(value);
    timeBottom_->SetStringSelection(wxString::Format("%d", s.timeBottom));

    bars_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 1, 2000, s.bars);
    perLine_ = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 1, 8, s.barsPerLine);

    auto* grid = new wxFlexGridSizer(4, 8, 12);
    grid->AddGrowableCol(1, 1);
    grid->AddGrowableCol(3, 1);
    const auto add = [&](const char* text, wxWindow* control) {
        grid->Add(Label(this, text), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(control, 1, wxEXPAND);
    };
    add("Title", title_);
    add("Subtitle", subtitle_);
    add("Composer", composer_);
    add("Tempo text", tempoText_);
    add("Key", key_);
    add("Tempo (quarter = )", tempo_);

    auto* timeRow = new wxBoxSizer(wxHORIZONTAL);
    timeRow->Add(timeTop_, 0, wxALIGN_CENTER_VERTICAL);
    timeRow->Add(Label(this, "/"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);
    timeRow->Add(timeBottom_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(Label(this, "Time signature"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(timeRow);
    add("Bars", bars_);
    add("Bars per line", perLine_);

    SetSizerAndFit(grid);
}

ProjectSettings SettingsForm::Get() const {
    ProjectSettings s;
    s.title = title_->GetValue();
    s.subtitle = subtitle_->GetValue();
    s.composer = composer_->GetValue();
    s.tempoText = tempoText_->GetValue();
    s.tempo = tempo_->GetValue();
    const int key = key_->GetSelection();
    s.minor = key >= 15;
    s.fifths = (key % 15) - 7;
    s.timeTop = timeTop_->GetValue();
    s.timeBottom = wxAtoi(timeBottom_->GetStringSelection());
    s.bars = bars_->GetValue();
    s.barsPerLine = perLine_->GetValue();
    return s;
}

ProjectSettingsDialog::ProjectSettingsDialog(wxWindow* parent, const ProjectSettings& current)
    : wxDialog(parent, wxID_ANY, "Project Settings") {
    SetBackgroundColour(kPanel);
    form_ = new SettingsForm(this, current);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(form_, 1, wxEXPAND | wxALL, 16);
    sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
    SetSizerAndFit(sizer);
    SetMinSize(wxSize(620, -1));
    Fit();
    CentreOnParent();
}

InstrumentPicker::InstrumentPicker(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundColour(kPanel);

    available_ = new wxListBox(this, wxID_ANY);
    const auto& catalog = InstrumentCatalog();
    wxString section;
    for (size_t i = 0; i < catalog.size(); ++i) {
        if (section != catalog[i].section) {
            section = catalog[i].section;
            available_->Append(section.Upper());
            availableMap_.push_back(-1);
        }
        available_->Append(wxString("    ") + catalog[i].name);
        availableMap_.push_back(int(i));
    }
    chosen_ = new wxListBox(this, wxID_ANY);
    auto* add = new wxButton(this, wxID_ANY, "Add >");
    auto* remove = new wxButton(this, wxID_ANY, "< Remove");

    auto* left = new wxBoxSizer(wxVERTICAL);
    left->Add(Label(this, "Available instruments"), 0, wxBOTTOM, 4);
    left->Add(available_, 1, wxEXPAND);
    auto* right = new wxBoxSizer(wxVERTICAL);
    right->Add(Label(this, "Your score, in orchestral order"), 0, wxBOTTOM, 4);
    right->Add(chosen_, 1, wxEXPAND);
    auto* buttons = new wxBoxSizer(wxVERTICAL);
    buttons->AddStretchSpacer();
    buttons->Add(add, 0, wxEXPAND | wxBOTTOM, 6);
    buttons->Add(remove, 0, wxEXPAND);
    buttons->AddStretchSpacer();
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(left, 1, wxEXPAND);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    sizer->Add(right, 1, wxEXPAND);
    SetSizer(sizer);

    add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddSelected(); });
    available_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { AddSelected(); });
    remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveSelected(); });
    chosen_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { RemoveSelected(); });
}

void InstrumentPicker::SetParts(std::vector<ScorePart> parts) {
    parts_ = std::move(parts);
    RefreshChosen();
}

void InstrumentPicker::AddSelected() {
    const int row = available_->GetSelection();
    if (row == wxNOT_FOUND || availableMap_[row] < 0) return;
    parts_.push_back({availableMap_[row], -1});
    RefreshChosen();
}

void InstrumentPicker::RemoveSelected() {
    const int row = chosen_->GetSelection();
    if (row == wxNOT_FOUND) return;
    parts_.erase(parts_.begin() + row);
    RefreshChosen();
    if (!parts_.empty()) chosen_->SetSelection(std::min(row, int(parts_.size()) - 1));
}

void InstrumentPicker::RefreshChosen() {
    SortParts(parts_);
    std::vector<int> indices;
    for (const ScorePart& part : parts_) indices.push_back(part.catalog);
    const Project built = BuildProject(ProjectSettings{}, indices);
    chosen_->Clear();
    for (const TrackDef& track : built.tracks) chosen_->Append(track.name);
    if (onChanged) onChanged();
}

ChangeInstrumentsDialog::ChangeInstrumentsDialog(wxWindow* parent, const std::vector<ScorePart>& current)
    : wxDialog(parent, wxID_ANY, "Add or Remove Instruments", wxDefaultPosition, wxSize(680, 560),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    SetBackgroundColour(kPanel);
    picker_ = new InstrumentPicker(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(Label(this, "Instruments you keep hold on to their notes and sounds. "
                           "Add one twice to get numbered parts."),
               0, wxALL, 16);
    sizer->Add(picker_, 1, wxEXPAND | wxLEFT | wxRIGHT, 16);
    sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 16);
    SetSizer(sizer);

    auto* ok = static_cast<wxButton*>(FindWindow(wxID_OK));
    picker_->onChanged = [this, ok] { ok->Enable(!picker_->GetParts().empty()); };
    picker_->SetParts(current);
    CentreOnParent();
}

NewProjectDialog::NewProjectDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "New Project", wxDefaultPosition, wxSize(860, 680),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      plugins_(ScanVst3Plugins()) {
    SetBackgroundColour(kPanel);

    templates_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(170, -1));
    for (const char* name : {"Full Orchestra", "Small Orchestra", "Soloist", "Custom"})
        templates_->Append(name);
    templates_->SetSelection(0);

    heading_ = Label(this, "", kText);
    preview_ = new wxListBox(this, wxID_ANY);

    wxArrayString soloNames;
    for (const InstrumentSpec& spec : InstrumentCatalog()) soloNames.Add(spec.name);
    soloist_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, soloNames);
    soloist_->SetSelection(FindInstrument("Piano"));

    picker_ = new InstrumentPicker(this);
    form_ = new SettingsForm(this, NewProjectDefaults());

    auto* right = new wxBoxSizer(wxVERTICAL);
    right->Add(heading_, 0, wxBOTTOM, 6);
    right->Add(soloist_, 0, wxEXPAND | wxBOTTOM, 6);
    right->Add(preview_, 1, wxEXPAND);
    right->Add(picker_, 1, wxEXPAND);

    topSizer_ = new wxBoxSizer(wxHORIZONTAL);
    topSizer_->Add(templates_, 0, wxEXPAND | wxRIGHT, 14);
    topSizer_->Add(right, 1, wxEXPAND);

    // Step two: a sound for each instrument.
    soundsPage_ = new wxPanel(this);
    soundsPage_->SetBackgroundColour(kPanel);
    wxArrayString soundNames;
    soundNames.Add("Placeholder sound");
    for (const PluginInfo& plugin : plugins_) soundNames.Add(plugin.name);
    setAll_ = new wxChoice(soundsPage_, wxID_ANY, wxDefaultPosition, wxSize(260, -1), soundNames);
    setAll_->SetSelection(0);
    soundRows_ = new wxScrolledWindow(soundsPage_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    soundRows_->SetBackgroundColour(kPanel);
    soundRows_->SetScrollRate(0, 16);

    auto* setAllRow = new wxBoxSizer(wxHORIZONTAL);
    setAllRow->Add(Label(soundsPage_, "Set every instrument to"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    setAllRow->Add(setAll_);
    auto* pageSizer = new wxBoxSizer(wxVERTICAL);
    pageSizer->Add(setAllRow, 0, wxBOTTOM, 12);
    pageSizer->Add(soundRows_, 1, wxEXPAND);
    pageSizer->Add(Label(soundsPage_, "Plugins load when the project is created. Sample libraries may need "
                                      "you to pick a patch afterwards, in the Instruments window."),
                   0, wxTOP, 10);
    soundsPage_->SetSizer(pageSizer);

    back_ = new wxButton(this, wxID_ANY, "< Back");
    next_ = new wxButton(this, wxID_ANY, "Next >");
    create_ = new wxButton(this, wxID_OK, "Create");
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(back_);
    buttons->AddStretchSpacer();
    buttons->Add(next_, 0, wxRIGHT, 8);
    buttons->Add(create_, 0, wxRIGHT, 8);
    buttons->Add(new wxButton(this, wxID_CANCEL, "Cancel"));
    SetDefaultItem(create_);

    divider_ = new wxStaticLine(this);
    sizer_ = new wxBoxSizer(wxVERTICAL);
    sizer_->Add(topSizer_, 1, wxEXPAND | wxALL, 16);
    sizer_->Add(soundsPage_, 1, wxEXPAND | wxALL, 16);
    sizer_->Add(divider_, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
    sizer_->Add(form_, 0, wxEXPAND | wxALL, 16);
    sizer_->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
    SetSizer(sizer_);

    templates_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdatePage(); });
    soloist_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdatePage(); });
    picker_->onChanged = [this] { UpdatePage(); };
    back_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowStep(false); });
    next_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowStep(true); });
    setAll_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        for (wxChoice* choice : soundChoices_) choice->SetSelection(setAll_->GetSelection());
    });

    ShowStep(false);
    CentreOnParent();
}

std::vector<int> NewProjectDialog::Instruments() const {
    switch (templates_->GetSelection()) {
        case kFull: return TemplateInstruments(ProjectTemplate::FullOrchestra, 0);
        case kSmall: return TemplateInstruments(ProjectTemplate::SmallOrchestra, 0);
        case kSoloist: return TemplateInstruments(ProjectTemplate::Soloist, soloist_->GetSelection());
        default: {
            std::vector<int> indices;
            for (const ScorePart& part : picker_->GetParts()) indices.push_back(part.catalog);
            return indices;
        }
    }
}

void NewProjectDialog::UpdatePage() {
    const int which = templates_->GetSelection();
    const bool custom = which == kCustom;
    soloist_->Show(which == kSoloist);
    preview_->Show(!custom);
    picker_->Show(custom);

    if (!custom) {
        const Project built = BuildProject(ProjectSettings{}, Instruments());
        preview_->Clear();
        for (const TrackDef& track : built.tracks) preview_->Append(track.name);
        heading_->SetLabel(wxString::Format("%zu instruments, in score order", built.tracks.size()));
    } else {
        heading_->SetLabel("Choose instruments. Add one twice to get numbered parts.");
    }
    const bool hasInstruments = !Instruments().empty();
    next_->Enable(hasInstruments);
    create_->Enable(hasInstruments);
    Layout();
}

void NewProjectDialog::ShowStep(bool sounds) {
    // Without any plugins there's nothing to assign, so the sounds step is skipped.
    const bool canAssign = !plugins_.empty();
    if (sounds) BuildSoundRows();

    sizer_->Show(topSizer_, !sounds);
    sizer_->Show(divider_, !sounds);
    sizer_->Show(form_, !sounds);
    sizer_->Show(soundsPage_, sounds);
    back_->Show(sounds);
    next_->Show(!sounds && canAssign);
    create_->Show(sounds || !canAssign);
    if (sounds || !canAssign) create_->SetDefault();

    SetTitle(sounds ? "New Project: Assign Sounds" : "New Project");
    if (!sounds) UpdatePage();
    Layout();
}

void NewProjectDialog::BuildSoundRows() {
    for (size_t i = 0; i < soundChoices_.size(); ++i)
        rememberedSounds_[soundChoices_[i]->GetName()] = soundChoices_[i]->GetSelection();
    soundChoices_.clear();
    soundRows_->DestroyChildren();

    const Project built = BuildProject(ProjectSettings{}, Instruments());
    wxArrayString names;
    names.Add("Placeholder sound");
    for (const PluginInfo& plugin : plugins_) names.Add(plugin.name);

    auto* grid = new wxFlexGridSizer(2, 8, 16);
    grid->AddGrowableCol(1, 1);
    for (const TrackDef& track : built.tracks) {
        auto* choice = new wxChoice(soundRows_, wxID_ANY, wxDefaultPosition, wxDefaultSize, names);
        choice->SetName(track.name);
        const auto remembered = rememberedSounds_.find(track.name);
        choice->SetSelection(remembered == rememberedSounds_.end() ? 0 : remembered->second);
        soundChoices_.push_back(choice);
        grid->Add(Label(soundRows_, track.name, kText), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(choice, 1, wxEXPAND | wxRIGHT, 12);
    }
    soundRows_->SetSizer(grid, true);
    soundRows_->FitInside();
    soundRows_->Scroll(0, 0);
}

Project NewProjectDialog::BuildResult() const {
    Project project = BuildProject(form_->Get(), Instruments());
    for (int t = 0; t < int(soundChoices_.size()) && t < int(project.tracks.size()); ++t) {
        const int selection = soundChoices_[t]->GetSelection();
        if (selection <= 0) continue;
        PluginSetup setup;
        setup.track = t;
        setup.path = plugins_[selection - 1].path;
        project.plugins.push_back(std::move(setup));
    }
    return project;
}
