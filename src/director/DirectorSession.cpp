#include "director/DirectorSession.h"

#include <algorithm>
#include <set>

#include "director/DirectorSettings.h"
#include "director/ScoreText.h"

using nlohmann::json;

namespace {

constexpr int kMaxTurns = 8;

const char* const kSystemPrompt = R"(You are the Director, a composer's assistant inside Composer, a music notation application. You help the user understand and improve the piece open in front of them. Be a thoughtful musical collaborator: consider voice leading, register, orchestration, and how a change sits against the other parts.

Each message begins with the project's current state and the user's selection. If they say "this" or "here", they mean the selection. Use read_score whenever you need bars you haven't been shown.

To change the music, call propose_edit. The user hears and reviews your proposal before anything changes, so describe what you did in its summary and don't ask permission first. For questions and discussion, just answer. Change only what was asked, and leave other bars and staves alone unless the request calls for them. You can edit notes, articulations, dynamics and hairpins. If asked to change tempo, key, time signature or the instrument list, say that you can't do that yet.

Notation used by read_score and propose_edit:
- A bar is a space-separated list of tokens: duration:pitches
- Durations: w whole, h half, q quarter, e eighth, s sixteenth. A trailing dot makes it dotted: h. q. e.
- Pitches: scientific names at sounding pitch, such as Eb4, F#5, C4 (middle C). Chords use commas: h:C4,Eb4,G4
- A rest is q:r. A whole bar of rest is the single token R.
- Articulations follow a slash after the pitches: q:Eb4/. is staccato. Codes: . staccato, > accent, - tenuto, ^ marcato, f fermata. They combine, as in q:Eb4/.>
- In read_score, text after | lists that staff's dynamics and hairpins in the bar.
- Beats in dynamics and hairpins count quarter notes from the start of the bar: 1 is the downbeat, 1.5 the off-beat, whatever the time signature.
- Bars can differ in time signature. Each bar is labelled with its own in read_score.
- Write the pitch you want heard. A note with no accidental is the natural note, so in E-flat major you must write Bb4, Eb5 and Ab4. Composer works out the printed accidentals.
- Each bar must add up exactly to the length of the bar. There are no ties or tuplets yet, so notes can't cross a barline.

Replies appear in a small plain-text chat panel, so write short plain paragraphs without Markdown.)";

json Tools() {
    static const json tools = json::parse(R"([
      {
        "name": "read_score",
        "description": "Read the notes of a range of bars. Omit staves to read every staff. Resting staves are left out.",
        "eager_input_streaming": true,
        "input_schema": {
          "type": "object",
          "properties": {
            "first_bar": {"type": "integer", "description": "First bar to read, numbered from 1."},
            "last_bar": {"type": "integer", "description": "Last bar to read, inclusive."},
            "staves": {"type": "array", "items": {"type": "string"}, "description": "Staff names exactly as listed in the project."}
          },
          "required": ["first_bar", "last_bar"]
        }
      },
      {
        "name": "propose_edit",
        "description": "Propose replacing whole bars of music. The user previews the proposal and decides whether to apply it. Ends your turn once accepted as valid.",
        "eager_input_streaming": true,
        "input_schema": {
          "type": "object",
          "properties": {
            "title": {"type": "string", "description": "A short name for the change, such as: Flute countermelody, bars 5-8."},
            "summary": {"type": "string", "description": "One or two sentences on what changed and why."},
            "changes": {
              "type": "array",
              "description": "One entry per staff per bar being replaced.",
              "items": {
                "type": "object",
                "properties": {
                  "staff": {"type": "string", "description": "Staff name exactly as listed in the project."},
                  "bar": {"type": "integer", "description": "Bar number, from 1."},
                  "notes": {"type": "string", "description": "The complete new contents of the bar."}
                },
                "required": ["staff", "bar", "notes"]
              }
            },
            "dynamics": {
              "type": "array",
              "description": "Dynamic marks to add. A new mark replaces any at the same spot; mark none removes one.",
              "items": {
                "type": "object",
                "properties": {
                  "staff": {"type": "string"},
                  "bar": {"type": "integer"},
                  "beat": {"type": "number"},
                  "mark": {"type": "string", "enum": ["pp", "p", "mp", "mf", "f", "ff", "none"]}
                },
                "required": ["staff", "bar", "beat", "mark"]
              }
            },
            "hairpins": {
              "type": "array",
              "description": "Crescendos and diminuendos. Type none removes the hairpin starting at that spot.",
              "items": {
                "type": "object",
                "properties": {
                  "staff": {"type": "string"},
                  "start_bar": {"type": "integer"},
                  "start_beat": {"type": "number"},
                  "end_bar": {"type": "integer"},
                  "end_beat": {"type": "number", "description": "Where it ends; one past the last beat means the end of the bar."},
                  "type": {"type": "string", "enum": ["crescendo", "diminuendo", "none"]}
                },
                "required": ["staff", "start_bar", "start_beat", "end_bar", "end_beat", "type"]
              }
            }
          },
          "required": ["title", "summary"]
        }
      }
    ])");
    return tools;
}

json TextBlock(const wxString& text) { return {{"type", "text"}, {"text", std::string(text.ToUTF8())}}; }

json ToolResult(const json& call, const wxString& text, bool isError) {
    json result = {{"type", "tool_result"}, {"tool_use_id", call.value("id", "")}, {"content", std::string(text.ToUTF8())}};
    if (isError) result["is_error"] = true;
    return result;
}

wxString Utf8Field(const json& object, const char* name) {
    return object.contains(name) && object[name].is_string()
               ? wxString::FromUTF8(object[name].get<std::string>())
               : wxString();
}

// After a mid-reply model fallback, what came before the switch must not be replayed or run.
void DropBlocksBeforeFallback(json& content) {
    int last = -1;
    for (int i = 0; i < int(content.size()); ++i)
        if (content[i].value("type", "") == "fallback") last = i;
    if (last < 0) return;

    json kept = json::array();
    for (int i = 0; i < int(content.size()); ++i) {
        const std::string type = content[i].value("type", "");
        if (i < last && (type == "thinking" || type == "redacted_thinking" || type == "tool_use")) continue;
        kept.push_back(content[i]);
    }
    content = std::move(kept);
}

}  // namespace

DirectorSession::DirectorSession(std::function<const Project&()> project) : project_(std::move(project)) {
    client_.onText = [this](const std::string& utf8) {
        if (needBreak_ && hadText_ && onText) onText("\n\n");
        needBreak_ = false;
        hadText_ = true;
        if (onText) onText(wxString::FromUTF8(utf8));
    };
    client_.onComplete = [this](json content, const std::string& stop, const DirectorUsage& usage) {
        OnComplete(std::move(content), stop, usage);
    };
    client_.onError = [this](int status, const wxString& message) { OnError(status, message); };
}

void DirectorSession::Reset() {
    client_.Cancel();
    messages_ = json::array();
    pendingResults_ = json::array();
    savedPending_ = json::array();
    hadText_ = needBreak_ = false;
    cost_ = 0;
}

void DirectorSession::Cancel() {
    if (!client_.IsBusy()) return;
    client_.Cancel();
    if (firstRequest_) RollBackUnansweredTurn();
}

void DirectorSession::Send(const wxString& message, const wxString& context, const wxString& apiKey,
                           const wxString& model) {
    apiKey_ = apiKey;
    model_ = model;
    turns_ = 0;
    hadText_ = needBreak_ = false;

    json content = pendingResults_;
    savedPending_ = pendingResults_;
    pendingResults_ = json::array();
    content.push_back(TextBlock(context + "\n\nUser's message:\n" + message));

    // A trailing user message means the last request never got a reply, so it can be extended.
    if (!messages_.empty() && messages_.back()["role"] == "user") {
        for (const json& block : content) messages_.back()["content"].push_back(block);
    } else {
        messages_.push_back({{"role", "user"}, {"content", std::move(content)}});
    }
    firstRequest_ = true;
    Request();
}

void DirectorSession::Request() {
    ++turns_;
    if (onStatus) onStatus(turns_ == 1 ? "Director is thinking" : "Director is working");

    json body = {
        {"model", std::string(model_.ToUTF8())},
        {"max_tokens", 64000},
        {"stream", true},
        {"system", kSystemPrompt},
        {"tools", Tools()},
        {"tool_choice", {{"type", "auto"}}},
        {"thinking", {{"type", "adaptive"}}},
        {"output_config", {{"effort", "high"}}},
        {"cache_control", {{"type", "ephemeral"}}},
        {"messages", messages_},
    };
    // Automatic refusal fallback isn't offered for Haiku.
    if (model_ != "claude-haiku-5-5") body["fallbacks"] = "default";
    client_.Send(body, apiKey_);
}

void DirectorSession::RollBackUnansweredTurn() {
    if (!messages_.empty() && messages_.back()["role"] == "user") {
        messages_.erase(messages_.size() - 1);
        pendingResults_ = savedPending_;
    }
}

void DirectorSession::AnswerUnfinishedTools(const json& content, const wxString& reason) {
    for (const json& block : content)
        if (block.value("type", "") == "tool_use") pendingResults_.push_back(ToolResult(block, reason, true));
}

void DirectorSession::Finish(int status, const wxString& error) {
    if (onFinished) onFinished(status, error);
}

void DirectorSession::OnError(int status, const wxString& message) {
    if (firstRequest_) RollBackUnansweredTurn();
    Finish(status, message.empty() ? wxString::Format("The request failed (status %d).", status) : message);
}

void DirectorSession::OnComplete(json content, const std::string& stopReason, const DirectorUsage& usage) {
    cost_ += DirectorCost(model_, usage.input, usage.output, usage.cacheRead, usage.cacheWrite);
    if (onCost) onCost(cost_);

    if (stopReason == "refusal") {
        if (firstRequest_) RollBackUnansweredTurn();
        Finish(0, "The Director declined that request. Try rephrasing it.");
        return;
    }

    DropBlocksBeforeFallback(content);
    firstRequest_ = false;
    messages_.push_back({{"role", "assistant"}, {"content", content}});

    if (stopReason == "max_tokens") {
        AnswerUnfinishedTools(content, "The reply was cut off before this finished.");
        Finish(0, "The reply was too long and got cut off. Try asking for a smaller change.");
        return;
    }
    if (stopReason != "tool_use") {
        Finish(0, wxString());
        return;
    }

    bool proposed = false;
    json results = json::array();
    for (const json& block : content)
        if (block.value("type", "") == "tool_use") results.push_back(RunTool(block, proposed));

    if (proposed) {
        // The user acts on the card next; these answers go out with their next message.
        pendingResults_ = std::move(results);
        Finish(0, wxString());
        return;
    }

    messages_.push_back({{"role", "user"}, {"content", std::move(results)}});
    if (turns_ >= kMaxTurns) {
        Finish(0, "The Director couldn't finish that within its limit. Try a smaller request.");
        return;
    }
    needBreak_ = true;
    Request();
}

json DirectorSession::RunTool(const json& call, bool& proposed) {
    const std::string name = call.value("name", "");
    if (call.value("invalid_json", false))
        return ToolResult(call, "INVALID_JSON: the input could not be parsed. Send the call again.", true);

    const json& input = call["input"];
    wxString error;
    if (name == "read_score") {
        const wxString text = ReadScore(input, error);
        return error.empty() ? ToolResult(call, text, false) : ToolResult(call, error, true);
    }
    if (name == "propose_edit") {
        Proposal proposal;
        if (!BuildProposal(input, proposal, error)) return ToolResult(call, error, true);
        proposed = true;
        if (onProposal) onProposal(proposal);
        return ToolResult(call, "The proposal is on screen for the user to preview and apply or discard.", false);
    }
    return ToolResult(call, "Unknown tool.", true);
}

wxString DirectorSession::ReadScore(const json& input, wxString& error) {
    const Project& project = project_();
    const int bars = project.settings.bars;
    if (!input.contains("first_bar") || !input["first_bar"].is_number_integer() ||
        !input.contains("last_bar") || !input["last_bar"].is_number_integer()) {
        error = "first_bar and last_bar are required integers.";
        return wxString();
    }
    const int first = input["first_bar"].get<int>();
    const int last = input["last_bar"].get<int>();
    if (first < 1 || last < first || last > bars) {
        error = wxString::Format("Bars must be within 1 to %d, with first_bar <= last_bar.", bars);
        return wxString();
    }

    std::vector<int> staves;
    if (input.contains("staves") && input["staves"].is_array()) {
        for (const json& entry : input["staves"]) {
            const wxString name = entry.is_string() ? wxString::FromUTF8(entry.get<std::string>()) : wxString();
            const int staff = FindStaffByName(project, name);
            if (staff < 0) {
                error = "There is no staff named \"" + name + "\".";
                return wxString();
            }
            staves.push_back(staff);
        }
    }

    if (onStatus) onStatus(wxString::Format("Reading bars %d-%d", first, last));
    return BarsToText(project, first - 1, last - 1, staves, true);
}

bool DirectorSession::BuildProposal(const json& input, Proposal& out, wxString& error) {
    const Project& project = project_();
    out.title = Utf8Field(input, "title");
    out.summary = Utf8Field(input, "summary");
    if (out.title.empty()) {
        error = "A title is required.";
        return false;
    }
    const auto listOf = [&](const char* name) {
        return input.contains(name) && input[name].is_array() ? input[name] : json::array();
    };
    const json changes = listOf("changes"), dynamics = listOf("dynamics"), hairpins = listOf("hairpins");
    if (changes.empty() && dynamics.empty() && hairpins.empty()) {
        error = "Include at least one of changes, dynamics or hairpins.";
        return false;
    }

    // Collect every problem so one correction round is enough.
    wxString problems;
    std::set<std::pair<int, int>> seen;
    for (const json& change : changes) {
        const wxString staffName = Utf8Field(change, "staff");
        const int staff = FindStaffByName(project, staffName);
        const bool hasBar = change.contains("bar") && change["bar"].is_number_integer();
        const int barNumber = hasBar ? change["bar"].get<int>() : 0;
        const wxString where = wxString::Format("Bar %d, ", barNumber) + staffName + ": ";

        if (staff < 0) {
            problems += "There is no staff named \"" + staffName + "\".\n";
            continue;
        }
        if (barNumber < 1 || barNumber > project.settings.bars) {
            problems += wxString::Format("Bar %d doesn't exist; the score has %d bars.\n", barNumber, project.settings.bars);
            continue;
        }
        const int bar = barNumber - 1;
        if (!seen.insert({staff, bar}).second) {
            problems += where + "changed more than once.\n";
            continue;
        }

        BarEdit edit;
        edit.staff = staff;
        edit.bar = bar;
        edit.original = project.score[staff][bar];
        wxString why;
        if (!ParseBarText(project, staff, bar, Utf8Field(change, "notes"), edit.replacement, &why)) {
            problems += where + why + ".\n";
            continue;
        }
        out.edits.push_back(std::move(edit));
    }

    const auto numberField = [](const json& object, const char* name, double fallback) {
        return object.contains(name) && object[name].is_number() ? object[name].get<double>() : fallback;
    };
    const auto intField = [](const json& object, const char* name) {
        return object.contains(name) && object[name].is_number_integer() ? object[name].get<int>() : 0;
    };

    for (const json& entry : dynamics) {
        const wxString staffName = Utf8Field(entry, "staff");
        const int staff = FindStaffByName(project, staffName);
        const int bar = intField(entry, "bar") - 1;
        const double beat = numberField(entry, "beat", 1) - 1;
        const wxString mark = Utf8Field(entry, "mark");
        if (staff < 0 || bar < 0 || bar >= project.settings.bars || beat < 0 || beat >= project.BarBeats(bar)) {
            problems += "Dynamic on \"" + staffName + wxString::Format("\" at bar %d is out of range.\n", bar + 1);
            continue;
        }
        if (mark != "none" && mark != "pp" && mark != "p" && mark != "mp" && mark != "mf" && mark != "f" && mark != "ff") {
            problems += "\"" + mark + "\" is not a dynamic.\n";
            continue;
        }
        Marking m{bar, staff, beat, mark};
        (mark == "none" ? out.removedDynamics : out.dynamics).push_back(std::move(m));
    }

    for (const json& entry : hairpins) {
        const wxString staffName = Utf8Field(entry, "staff");
        Hairpin h;
        h.staff = FindStaffByName(project, staffName);
        h.bar = intField(entry, "start_bar") - 1;
        h.beat = numberField(entry, "start_beat", 1) - 1;
        h.endBar = intField(entry, "end_bar") - 1;
        h.endBeat = numberField(entry, "end_beat", 1) - 1;
        const wxString type = Utf8Field(entry, "type");
        const int bars = project.settings.bars;
        const bool inRange = h.staff >= 0 && h.bar >= 0 && h.bar < bars && h.beat >= 0 && h.beat < project.BarBeats(h.bar);
        if (!inRange) {
            problems += "Hairpin on \"" + staffName + wxString::Format("\" at bar %d is out of range.\n", h.bar + 1);
            continue;
        }
        if (type == "none") {
            out.removedHairpins.push_back(h);
            continue;
        }
        if (type != "crescendo" && type != "diminuendo") {
            problems += "\"" + type + "\" is not a hairpin type.\n";
            continue;
        }
        if (h.endBar < 0 || h.endBar >= bars || h.endBeat < 0 || h.endBeat > project.BarBeats(h.endBar) + 1e-6 ||
            project.BarStart(h.endBar) + h.endBeat <= project.BarStart(h.bar) + h.beat) {
            problems += "Hairpin on \"" + staffName + wxString::Format("\" at bar %d must end after it starts, within the score.\n", h.bar + 1);
            continue;
        }
        h.crescendo = type == "crescendo";
        out.hairpins.push_back(h);
    }

    if (!problems.empty()) {
        error = "The proposal was not shown. Fix these and call propose_edit again with all changes:\n" + problems;
        return false;
    }
    return true;
}
