// The Director: a simulated chat assistant that proposes changes to the score.
#pragma once

#include <wx/scrolwin.h>
#include <wx/timer.h>

#include <functional>
#include <vector>

#include "ScoreCanvas.h"
#include "Theme.h"

class ChatView;
class DirectorFooter;

class DirectorPanel : public wxPanel {
public:
    explicit DirectorPanel(wxWindow* parent);

    void SetScope(int bar, int staff);

    std::function<void(ProposalState)> onProposal;

private:
    void Send(const wxString& text);
    void Reply();

    ChatView* chat_;
    DirectorFooter* footer_;
    wxTextCtrl* input_;
    wxTimer replyTimer_;
    wxString pendingPrompt_;
    wxString scope_ = "Entire score";
};
