// One conversation with the Director: history, the tool loop and proposal validation.
#pragma once

#include <functional>
#include <nlohmann/json.hpp>

#include "Project.h"
#include "director/DirectorClient.h"

class DirectorSession {
public:
    // Supplies the document fresh on each tool call, so results always reflect the live score.
    explicit DirectorSession(std::function<const Project&()> project);

    void Send(const wxString& message, const wxString& context, const wxString& apiKey, const wxString& model);
    void Cancel();
    void Reset();
    bool IsBusy() const { return client_.IsBusy(); }

    std::function<void(const wxString& text)> onText;
    std::function<void(const wxString& status)> onStatus;
    std::function<void(const Proposal& proposal)> onProposal;
    std::function<void(double dollars)> onCost;
    // Called once per request; an empty message means it finished normally.
    std::function<void(int status, const wxString& error)> onFinished;

private:
    void Request();
    void OnComplete(nlohmann::json content, const std::string& stopReason, const DirectorUsage& usage);
    void OnError(int status, const wxString& message);
    void Finish(int status, const wxString& error);
    void RollBackUnansweredTurn();
    void AnswerUnfinishedTools(const nlohmann::json& content, const wxString& reason);
    nlohmann::json RunTool(const nlohmann::json& call, bool& proposed);
    wxString ReadScore(const nlohmann::json& input, wxString& error);
    bool BuildProposal(const nlohmann::json& input, Proposal& out, wxString& error);

    std::function<const Project&()> project_;
    DirectorClient client_;
    nlohmann::json messages_ = nlohmann::json::array();
    nlohmann::json pendingResults_ = nlohmann::json::array();  // answers held for the next message
    nlohmann::json savedPending_ = nlohmann::json::array();
    wxString apiKey_, model_;
    int turns_ = 0;
    bool firstRequest_ = false, hadText_ = false, needBreak_ = false;
    double cost_ = 0;
};
