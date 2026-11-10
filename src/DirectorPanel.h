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
    void SetScope(const BarRange& range);
    void ResetConversation(const wxString& projectTitle);
    void ShowSampleConversation(const Proposal& proposal);
    void CancelRequest();
    void ScoreChanged();       // proposals may no longer match the score
    void PreviewEnded();       // the score left a preview by itself
    void InvalidateProposals();  // staff numbering changed, so none can be applied
    void RefreshModelName();
    // Sends a message on the user's behalf with extra material, such as a sung melody, added to
    // the context. Returns false if a reply is still in progress.
    bool SendWithContext(const wxString& message, const wxString& extraContext);

    // proposal is null when the state is None.
    std::function<void(const Proposal* proposal, ProposalState state)> onProposal;
    std::function<void()> onOpenSettings;

private:
    void Send(const wxString& text, const wxString& extraContext = wxEmptyString);
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
    BarRange scopeRange_;
    double cost_ = 0;
};
