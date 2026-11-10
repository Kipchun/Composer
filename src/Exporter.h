// Writes the score and its parts to PDF, WAV and MP3 files.
#pragma once

#include <wx/wx.h>

#include <vector>

#include "Project.h"

class AudioEngine;

struct ExportOptions {
    wxString folder;
    bool fullScore = true;
    std::vector<int> partTracks;
    bool pdf = true, wav = true, mp3 = true;
};

// The files these options will produce, so the caller can check for existing ones.
std::vector<wxString> ExportFilePaths(const Project& project, const ExportOptions& options);

// Shows progress and returns false if cancelled or failed; error is empty when cancelled.
bool RunExport(wxWindow* parent, const Project& project, AudioEngine& engine, const ExportOptions& options,
               wxString* error);

class wxCheckListBox;
class wxDirPickerCtrl;

class ExportDialog : public wxDialog {
public:
    ExportDialog(wxWindow* parent, const Project& project, const wxString& startFolder);
    ExportOptions GetOptions() const;

private:
    void UpdateState();

    wxCheckBox* fullScore_;
    wxCheckBox* parts_;
    wxCheckListBox* partList_;
    wxCheckBox* pdf_;
    wxCheckBox* wav_;
    wxCheckBox* mp3_;
    wxDirPickerCtrl* folder_;
    wxButton* export_ = nullptr;
};
