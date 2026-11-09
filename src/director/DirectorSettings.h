// The Director's API key, kept in the system credential store, and its chosen model.
#pragma once

#include <wx/wx.h>

wxString LoadDirectorKey();
wxString LoadDirectorModel();
wxString DirectorModelName(const wxString& modelId);
// Approximate US dollars for the given token counts.
double DirectorCost(const wxString& modelId, long input, long output, long cacheRead, long cacheWrite);

// Returns true if the user pressed Save.
bool ShowDirectorSettings(wxWindow* parent);
