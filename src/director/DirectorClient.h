// Sends one streaming Messages API request and reassembles the reply.
#pragma once

#include <wx/timer.h>
#include <wx/webrequest.h>
#include <wx/wx.h>

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

struct DirectorUsage {
    long input = 0, output = 0, cacheRead = 0, cacheWrite = 0;
};

class DirectorClient : public wxEvtHandler {
public:
    DirectorClient();

    void Send(const nlohmann::json& body, const wxString& apiKey);
    void Cancel();
    bool IsBusy() const { return active_; }

    std::function<void(const std::string& utf8)> onText;
    std::function<void(nlohmann::json content, const std::string& stopReason, const DirectorUsage&)> onComplete;
    std::function<void(int status, const wxString& message)> onError;

    static bool IsMock();

private:
    void Start();
    void OnState(wxWebRequestEvent& event);
    void OnData(wxWebRequestEvent& event);
    void Feed(const char* data, size_t size);
    void HandleEvent(const nlohmann::json& event);
    void Finish(int status, const wxString& networkError, const wxString& retryAfter);
    void Fail(int status, const wxString& message);
    void RunMock();

    wxWebRequest request_;
    wxTimer retryTimer_, mockTimer_;
    std::string bodyText_;
    wxString apiKey_;
    bool useFallbacks_ = false;
    int generation_ = 1000;
    bool active_ = false, retried_ = false, sawStop_ = false;

    std::string buffer_, raw_;
    nlohmann::json content_;
    std::vector<std::string> partialInputs_;
    std::string stopReason_, streamError_;
    DirectorUsage usage_;
    int mockTurn_ = 0;
};
