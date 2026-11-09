#include "director/DirectorClient.h"

#include <wx/ffile.h>
#include <wx/utils.h>

#include <algorithm>

using nlohmann::json;

namespace {

const char* const kUrl = "https://api.anthropic.com/v1/messages";

wxString MockDirectory() {
    wxString dir;
    wxGetEnv("COMPOSER_DIRECTOR_MOCK", &dir);
    return dir;
}

std::string ReadFile(const wxString& path, bool& ok) {
    wxFFile file(path, "rb");
    std::string text;
    ok = file.IsOpened();
    if (ok) {
        text.resize(size_t(file.Length()));
        if (!text.empty()) ok = file.Read(text.data(), text.size()) == text.size();
    }
    return text;
}

}  // namespace

DirectorClient::DirectorClient() : retryTimer_(this), mockTimer_(this) {
    Bind(wxEVT_WEBREQUEST_STATE, &DirectorClient::OnState, this);
    Bind(wxEVT_WEBREQUEST_DATA, &DirectorClient::OnData, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent& e) {
        if (e.GetId() == retryTimer_.GetId()) Start();
        else RunMock();
    });
}

bool DirectorClient::IsMock() { return !MockDirectory().empty(); }

void DirectorClient::Send(const json& body, const wxString& apiKey) {
    bodyText_ = body.dump();
    apiKey_ = apiKey;
    useFallbacks_ = body.contains("fallbacks");
    retried_ = false;
    active_ = true;
    Start();
}

void DirectorClient::Start() {
    ++generation_;
    buffer_.clear();
    raw_.clear();
    content_ = json::array();
    partialInputs_.clear();
    stopReason_.clear();
    streamError_.clear();
    usage_ = DirectorUsage{};
    sawStop_ = false;

    if (IsMock()) {
        // Tests can inspect exactly what would have been sent.
        wxFFile dump(wxString::Format("%s/request_%d.json", MockDirectory(), mockTurn_ + 1), "wb");
        if (dump.IsOpened()) dump.Write(bodyText_.data(), bodyText_.size());
        wxString delay;
        wxGetEnv("COMPOSER_DIRECTOR_MOCK_DELAY", &delay);
        mockTimer_.StartOnce(delay.empty() ? 150 : wxAtoi(delay));
        return;
    }

    request_ = wxWebSession::GetDefault().CreateRequest(this, kUrl, generation_);
    if (!request_.IsOk()) {
        Fail(0, "This system couldn't start a network request.");
        return;
    }
    request_.SetMethod("POST");
    request_.SetHeader("x-api-key", apiKey_);
    request_.SetHeader("anthropic-version", "2026-10-21");
    if (useFallbacks_) request_.SetHeader("anthropic-beta", "server-side-fallback-2026-07-01");
    request_.SetData(wxString::FromUTF8(bodyText_), "application/json", wxConvUTF8);
    request_.SetStorage(wxWebRequest::Storage_None);
    request_.Start();
}

void DirectorClient::RunMock() {
    if (!active_) return;
    ++mockTurn_;
    bool ok = false;
    const std::string text = ReadFile(wxString::Format("%s/turn%d.sse", MockDirectory(), mockTurn_), ok);
    if (!ok) {
        Fail(0, wxString::Format("Mock mode has no reply for turn %d.", mockTurn_));
        return;
    }
    if (text.rfind("HTTP ", 0) == 0) {  // "HTTP 401" simulates a failed request
        Finish(atoi(text.c_str() + 5), wxEmptyString, wxEmptyString);
        return;
    }
    Feed(text.data(), text.size());
    Finish(200, wxEmptyString, wxEmptyString);
}

void DirectorClient::Cancel() {
    active_ = false;
    retryTimer_.Stop();
    mockTimer_.Stop();
    ++generation_;
    if (request_.IsOk()) request_.Cancel();
    request_ = wxWebRequest();
}

void DirectorClient::OnData(wxWebRequestEvent& event) {
    if (!active_ || event.GetId() != generation_) return;
    Feed(static_cast<const char*>(event.GetDataBuffer()), event.GetDataSize());
}

void DirectorClient::OnState(wxWebRequestEvent& event) {
    if (!active_ || event.GetId() != generation_) return;
    const wxWebRequest::State state = event.GetState();
    if (state != wxWebRequest::State_Completed && state != wxWebRequest::State_Failed &&
        state != wxWebRequest::State_Unauthorized)
        return;

    const wxWebResponse response = event.GetResponse();
    const int status = response.IsOk() ? response.GetStatus() : 0;
    Finish(status, event.GetErrorDescription(), response.IsOk() ? response.GetHeader("retry-after") : wxString());
}

void DirectorClient::Feed(const char* data, size_t size) {
    if (raw_.size() < 65536) raw_.append(data, size);
    buffer_.append(data, size);

    // Lines are complete UTF-8, so a character split across chunks is never decoded early.
    size_t start = 0, end;
    while ((end = buffer_.find('\n', start)) != std::string::npos) {
        std::string line = buffer_.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("data:", 0) != 0) continue;
        const json parsed = json::parse(line.begin() + 5, line.end(), nullptr, false);
        if (!parsed.is_discarded()) HandleEvent(parsed);
    }
    buffer_.erase(0, start);
}

void DirectorClient::HandleEvent(const json& event) {
    const std::string type = event.value("type", "");

    if (type == "message_start") {
        const json usage = event["message"].value("usage", json::object());
        usage_.input = usage.value("input_tokens", 0L);
        usage_.cacheRead = usage.value("cache_read_input_tokens", 0L);
        usage_.cacheWrite = usage.value("cache_creation_input_tokens", 0L);
    } else if (type == "content_block_start") {
        content_.push_back(event["content_block"]);
        partialInputs_.emplace_back();
    } else if (type == "content_block_delta") {
        const size_t index = event.value("index", size_t(0));
        if (index >= content_.size()) return;
        const json& delta = event["delta"];
        const std::string deltaType = delta.value("type", "");
        if (deltaType == "text_delta") {
            const std::string text = delta.value("text", "");
            content_[index]["text"] = content_[index].value("text", "") + text;
            if (onText && active_) onText(text);
        } else if (deltaType == "thinking_delta") {
            content_[index]["thinking"] = content_[index].value("thinking", "") + delta.value("thinking", "");
        } else if (deltaType == "signature_delta") {
            content_[index]["signature"] = delta.value("signature", "");
        } else if (deltaType == "input_json_delta") {
            partialInputs_[index] += delta.value("partial_json", "");
        }
    } else if (type == "content_block_stop") {
        const size_t index = event.value("index", size_t(0));
        if (index >= content_.size() || content_[index].value("type", "") != "tool_use") return;
        // Tool input arrives unvalidated; a bad parse is reported back to the model.
        const std::string& partial = partialInputs_[index];
        const json input = partial.empty() ? json::object() : json::parse(partial, nullptr, false);
        if (input.is_object()) {
            content_[index]["input"] = input;
        } else {
            content_[index]["input"] = json::object();
            content_[index]["invalid_json"] = true;
        }
    } else if (type == "message_delta") {
        stopReason_ = event["delta"].value("stop_reason", stopReason_);
        if (event.contains("usage")) usage_.output = event["usage"].value("output_tokens", usage_.output);
    } else if (type == "message_stop") {
        sawStop_ = true;
    } else if (type == "error") {
        streamError_ = event["error"].value("message", "The service reported an error.");
    }
}

void DirectorClient::Finish(int status, const wxString& networkError, const wxString& retryAfter) {
    if (status == 200 && streamError_.empty() && sawStop_) {
        active_ = false;
        request_ = wxWebRequest();
        if (onComplete) onComplete(std::move(content_), stopReason_, usage_);
        return;
    }

    const bool retryable = status == 429 || status >= 500;
    if (retryable && !retried_ && !sawStop_) {
        retried_ = true;
        long seconds = 2;
        retryAfter.ToLong(&seconds);
        retryTimer_.StartOnce(int(std::clamp(seconds, 1L, 30L)) * 1000);
        return;
    }

    wxString message;
    if (!streamError_.empty()) {
        message = wxString::FromUTF8(streamError_);
    } else if (status == 200) {
        message = "The connection ended before the reply finished.";
    } else if (status == 0) {
        message = "Couldn't reach the service. Check your internet connection." +
                  (networkError.empty() ? wxString() : " (" + networkError + ")");
    } else {
        const json parsed = json::parse(raw_, nullptr, false);
        if (parsed.is_object() && parsed.contains("error"))
            message = wxString::FromUTF8(parsed["error"].value("message", ""));
    }
    Fail(status, message);
}

void DirectorClient::Fail(int status, const wxString& message) {
    active_ = false;
    request_ = wxWebRequest();
    if (onError) onError(status, message);
}
