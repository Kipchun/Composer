// Converts the score to and from the pitch-name notation the Director reads and writes.
#pragma once

#include <vector>

#include "Project.h"

// Notation: "q:Eb4 e:F4 e:G4 h:C4,Eb4,G4 q:r", or "R" for a whole bar of rest.
// Durations are w h q e s with an optional trailing dot. Pitches are sounding pitches.
wxString BarToText(const Project& project, int staff, const Bar& bar);
bool ParseBarText(const Project& project, int staff, const wxString& text, Bar& out, wxString* error);

// A summary of the project for the model: settings and staff names.
wxString DescribeProject(const Project& project);

// Bar numbers are zero-based. Empty `staves` means all; skipEmpty leaves out resting staves.
wxString BarsToText(const Project& project, int firstBar, int lastBar, const std::vector<int>& staves,
                    bool skipEmpty);

int FindStaffByName(const Project& project, const wxString& name);  // -1 if unknown
