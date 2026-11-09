// The Director: chat with the AI assistant and review of the changes it proposes.
#pragma once

#include <wx/scrolwin.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <vector>

#include "ScoreCanvas.h"
#include "Theme.h"

class ChatView;
class DirectorFooter;
class DirectorHeader;
class SendButton;
class DirectorSession;

class DirectorPanel : public wxPanel {
public:
    explicit DirectorPanel(wxWindow* parent);
    ~DirectorPanel() override;

    void SetScore(const ScoreCanvas* score) { score_ = score; }
    void SetScope(int bar, const wxString& staffName);
    void ResetConversation(const wxString& projectTitle);
    void ShowSampleConversation(const Proposal& proposal);
    void CancelRequest();
    void ScoreChanged();       // proposals may no longer match the score
    void InvalidateProposals();  // staff numbering changed, so none can be applied
    void RefreshModelName();

    // proposal is null when the state is None.
    std::function<void(const Proposal* proposal, ProposalState state)> onProposal;
    std::function<void()> onOpenSettings;

private:
    void Send(const wxString& text);
    void SetBusy(bool busy);
    wxString BuildContext() const;

    const ScoreCanvas* score_ = nullptr;
    std::unique_ptr<DirectorSession> session_;
    DirectorHeader* header_;
    ChatView* chat_;
    DirectorFooter* footer_;
    wxTextCtrl* input_;
    SendButton* send_;
    wxString scope_ = "Entire score";
    int scopeBar_ = -1;
    wxString scopeStaff_;
    double cost_ = 0;
};
