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

To change the piece, call propose_edit. The user hears and reviews your proposal before anything changes, so describe what you did in its summary and don't ask permission first. For questions and discussion, just answer. Change only what was asked, and leave other bars and staves alone unless the request calls for them.

One proposal can change many staves and bars together, and should: put a whole orchestration, or a rewrite plus its tempo or meter change, in a single call so the user reviews it as one idea. Use staves in place of staff to write identical content on several staves. You can edit notes, articulations, dynamics, hairpins, time signatures, tempo and tempo changes, the key, rehearsal marks, the number of bars, bars per line, and the title, subtitle and composer. If asked to change the instrument list, say that you can't do that yet.

Bar numbers in a proposal always refer to the piece as it will be after the proposal. If a time signature change makes a bar shorter and that bar has music, rewrite that bar in the same proposal. Changing the key keeps notes on the same staff lines, so their pitches change; to keep or transpose the music, rewrite the bars in the same proposal.

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
                  "staves": {"type": "array", "items": {"type": "string"}, "description": "Use instead of staff to write the same notes on several staves."},
                  "bar": {"type": "integer", "description": "Bar number, from 1."},
                  "notes": {"type": "string", "description": "The complete new contents of the bar."}
                },
                "required": ["bar", "notes"]
              }
            },
            "dynamics": {
              "type": "array",
              "description": "Dynamic marks to add. A new mark replaces any at the same spot; mark none removes one.",
              "items": {
                "type": "object",
                "properties": {
                  "staff": {"type": "string"},
                  "staves": {"type": "array", "items": {"type": "string"}},
                  "bar": {"type": "integer"},
                  "beat": {"type": "number"},
                  "mark": {"type": "string", "enum": ["pp", "p", "mp", "mf", "f", "ff", "none"]}
                },
                "required": ["bar", "beat", "mark"]
              }
            },
            "hairpins": {
              "type": "array",
              "description": "Crescendos and diminuendos. Type none removes the hairpin starting at that spot.",
              "items": {
                "type": "object",
                "properties": {
                  "staff": {"type": "string"},
                  "staves": {"type": "array", "items": {"type": "string"}},
                  "start_bar": {"type": "integer"},
                  "start_beat": {"type": "number"},
                  "end_bar": {"type": "integer"},
                  "end_beat": {"type": "number", "description": "Where it ends; one past the last beat means the end of the bar."},
                  "type": {"type": "string", "enum": ["crescendo", "diminuendo", "none"]}
                },
                "required": ["start_bar", "start_beat", "end_bar", "end_beat", "type"]
              }
            },
            "time_signatures": {
              "type": "array",
              "description": "Set or remove a time signature from a bar until the next one. Bar 1 sets the opening and can't be removed.",
              "items": {
                "type": "object",
                "properties": {
                  "bar": {"type": "integer"},
                  "top": {"type": "integer", "description": "1 to 16."},
                  "bottom": {"type": "integer", "enum": [2, 4, 8]},
                  "remove": {"type": "boolean"}
                },
                "required": ["bar"]
              }
            },
            "tempos": {
              "type": "array",
              "description": "Set or remove a tempo from a bar until the next one. Bar 1 sets the opening tempo and can't be removed.",
              "items": {
                "type": "object",
                "properties": {
                  "bar": {"type": "integer"},
                  "bpm": {"type": "integer", "description": "Quarter notes per minute, 20 to 300."},
                  "text": {"type": "string", "description": "Optional, such as Allegro."},
                  "remove": {"type": "boolean"}
                },
                "required": ["bar"]
              }
            },
            "rehearsal_marks": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "bar": {"type": "integer"},
                  "label": {"type": "string", "description": "Short, usually one letter."},
                  "remove": {"type": "boolean"}
                },
                "required": ["bar"]
              }
            },
            "project": {
              "type": "object",
              "description": "Overall settings. Include only what should change.",
              "properties": {
                "title": {"type": "string"},
                "subtitle": {"type": "string"},
                "composer": {"type": "string"},
                "tempo_text": {"type": "string"},
                "key": {"type": "string", "description": "Such as Eb major or F# minor."},
                "bars": {"type": "integer", "description": "New total bars, 1 to 2000. Bars with music can't be removed this way."},
                "bars_per_line": {"type": "integer", "description": "1 to 8."}
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
    const json meters = listOf("time_signatures"), tempos = listOf("tempos"), marks = listOf("rehearsal_marks");
    const json settings = input.contains("project") && input["project"].is_object() ? input["project"] : json::object();
    if (changes.empty() && dynamics.empty() && hairpins.empty() && meters.empty() && tempos.empty() && marks.empty() &&
        settings.empty()) {
        error = "The proposal doesn't change anything.";
        return false;
    }

    const auto numberField = [](const json& object, const char* name, double fallback) {
        return object.contains(name) && object[name].is_number() ? object[name].get<double>() : fallback;
    };
    const auto intField = [](const json& object, const char* name) {
        return object.contains(name) && object[name].is_number_integer() ? object[name].get<int>() : 0;
    };
    const auto isRemoval = [](const json& object) { return object.value("remove", false); };

    wxString problems, details;
    const auto note = [&](const wxString& line) { details += (details.empty() ? "" : "\n") + line; };

    // Staves named by either form, reporting any that don't exist.
    const auto stavesOf = [&](const json& entry) {
        std::vector<int> staves;
        const auto add = [&](const wxString& name) {
            const int staff = FindStaffByName(project, name);
            if (staff < 0) problems += "There is no staff named \"" + name + "\".\n";
            else if (std::find(staves.begin(), staves.end(), staff) == staves.end()) staves.push_back(staff);
        };
        if (entry.contains("staff") && entry["staff"].is_string()) add(Utf8Field(entry, "staff"));
        if (entry.contains("staves") && entry["staves"].is_array())
            for (const json& name : entry["staves"])
                if (name.is_string()) add(wxString::FromUTF8(name.get<std::string>()));
        if (staves.empty() && !entry.contains("staff") && !entry.contains("staves"))
            problems += "An entry names no staff.\n";
        return staves;
    };

    // Whole-project settings come first, so bars can be checked against the result.
    if (settings.contains("title")) { out.title_ = Utf8Field(settings, "title"); note("Title: " + *out.title_); }
    if (settings.contains("subtitle")) { out.subtitle = Utf8Field(settings, "subtitle"); note("Subtitle: " + *out.subtitle); }
    if (settings.contains("composer")) { out.composer = Utf8Field(settings, "composer"); note("Composer: " + *out.composer); }
    if (settings.contains("tempo_text")) { out.tempoText = Utf8Field(settings, "tempo_text"); note("Tempo text: " + *out.tempoText); }
    if (settings.contains("key")) {
        const wxString wanted = Utf8Field(settings, "key");
        bool found = false;
        for (int minor = 0; minor < 2 && !found; ++minor) {
            for (int fifths = -7; fifths <= 7 && !found; ++fifths) {
                if (PlainKeyLabel(fifths, minor != 0).CmpNoCase(wanted) == 0) {
                    out.fifths = fifths;
                    out.minor = minor != 0;
                    found = true;
                }
            }
        }
        if (found) note("Key: " + wanted);
        else problems += "\"" + wanted + "\" is not a key; use a name like Eb major or F# minor.\n";
    }
    if (settings.contains("bars")) {
        const int bars = intField(settings, "bars");
        if (bars < 1 || bars > 2000) problems += "bars must be 1 to 2000.\n";
        else {
            out.bars = bars;
            note(wxString::Format("Length: %d bars", bars));
        }
    }
    if (settings.contains("bars_per_line")) {
        const int perLine = intField(settings, "bars_per_line");
        if (perLine < 1 || perLine > 8) problems += "bars_per_line must be 1 to 8.\n";
        else {
            out.barsPerLine = perLine;
            note(wxString::Format("%d bars per line", perLine));
        }
    }

    const int barTotal = out.bars.value_or(project.settings.bars);
    for (const json& entry : meters) {
        const int bar = intField(entry, "bar") - 1;
        if (bar < 0 || bar >= barTotal) {
            problems += wxString::Format("Time signature at bar %d is out of range.\n", bar + 1);
        } else if (isRemoval(entry)) {
            if (bar == 0) problems += "The opening time signature can't be removed; set a different one.\n";
            else {
                out.removedMeters.push_back(bar);
                note(wxString::Format("Remove time signature at bar %d", bar + 1));
            }
        } else {
            const int top = intField(entry, "top"), bottom = intField(entry, "bottom");
            if (top < 1 || top > 16 || (bottom != 2 && bottom != 4 && bottom != 8)) {
                problems += wxString::Format("Time signature at bar %d needs top 1 to 16 and bottom 2, 4 or 8.\n", bar + 1);
            } else {
                out.setMeters.push_back({bar, top, bottom});
                note(wxString::Format("%d/%d from bar %d", top, bottom, bar + 1));
            }
        }
    }
    for (const json& entry : tempos) {
        const int bar = intField(entry, "bar") - 1;
        if (bar < 0 || bar >= barTotal) {
            problems += wxString::Format("Tempo at bar %d is out of range.\n", bar + 1);
        } else if (isRemoval(entry)) {
            if (bar == 0) problems += "The opening tempo can't be removed; set a different one.\n";
            else {
                out.removedTempos.push_back(bar);
                note(wxString::Format("Remove tempo change at bar %d", bar + 1));
            }
        } else {
            TempoChange change;
            change.bar = bar;
            change.bpm = intField(entry, "bpm");
            change.text = Utf8Field(entry, "text");
            if (change.bpm < 20 || change.bpm > 300) {
                problems += wxString::Format("Tempo at bar %d needs bpm from 20 to 300.\n", bar + 1);
            } else {
                note(wxString::Format("Tempo %d from bar %d", change.bpm, bar + 1));
                out.setTempos.push_back(std::move(change));
            }
        }
    }
    for (const json& entry : marks) {
        const int bar = intField(entry, "bar") - 1;
        const wxString label = Utf8Field(entry, "label");
        if (bar < 0 || bar >= barTotal) problems += wxString::Format("Rehearsal mark at bar %d is out of range.\n", bar + 1);
        else if (isRemoval(entry)) { out.removedSections.push_back(bar); note(wxString::Format("Remove rehearsal mark at bar %d", bar + 1)); }
        else if (label.empty()) problems += wxString::Format("Rehearsal mark at bar %d needs a label.\n", bar + 1);
        else { out.setSections.push_back({bar, label}); note("Rehearsal mark " + label + wxString::Format(" at bar %d", bar + 1)); }
    }

    if (!problems.empty()) {
        error = "The proposal was not shown. Fix these and call propose_edit again with everything:\n" + problems;
        return false;
    }

    // Notes and marks are checked against the piece as it will be.
    const Project probe = ProjectWithProposal(project, out, false);

    std::set<std::pair<int, int>> seen;
    int minBar = probe.settings.bars, maxBar = -1;
    std::set<int> touchedStaves;
    for (const json& change : changes) {
        const std::vector<int> staves = stavesOf(change);
        const int barNumber = intField(change, "bar");
        if (barNumber < 1 || barNumber > probe.settings.bars) {
            problems += wxString::Format("Bar %d doesn't exist; the score will have %d bars.\n", barNumber, probe.settings.bars);
            continue;
        }
        const int bar = barNumber - 1;
        for (int staff : staves) {
            const wxString where = wxString::Format("Bar %d, ", barNumber) + StaffLabel(project, staff) + ": ";
            if (!seen.insert({staff, bar}).second) {
                problems += where + "changed more than once.\n";
                continue;
            }
            BarEdit edit;
            edit.staff = staff;
            edit.bar = bar;
            edit.original = bar < project.settings.bars ? project.score[staff][bar] : Bar{};
            wxString why;
            if (!ParseBarText(probe, staff, bar, Utf8Field(change, "notes"), edit.replacement, &why)) {
                problems += where + why + ".\n";
                continue;
            }
            out.edits.push_back(std::move(edit));
            minBar = std::min(minBar, bar);
            maxBar = std::max(maxBar, bar);
            touchedStaves.insert(staff);
        }
    }

    // Structural changes must never silently delete music.
    for (int staff = 0; staff < int(project.staves.size()); ++staff) {
        for (int bar = 0; bar < project.settings.bars; ++bar) {
            if (seen.count({staff, bar})) continue;
            const bool removed = bar >= probe.settings.bars;
            if (!removed && std::abs(project.BarBeats(bar) - probe.BarBeats(bar)) < 1e-6) continue;
            const double limit = removed ? 0 : probe.BarBeats(bar);
            double pos = 0;
            for (const Event& ev : project.score[staff][bar]) {
                if (!ev.rest && pos + ev.beats > limit + 1e-6) {
                    problems += wxString::Format("Bar %d, ", bar + 1) + StaffLabel(project, staff) +
                                (removed ? ": has music but would be removed.\n"
                                         : ": has notes past the new barline, so rewrite this bar in changes.\n");
                    break;
                }
                pos += ev.beats;
            }
        }
    }

    for (const json& entry : dynamics) {
        const std::vector<int> staves = stavesOf(entry);
        const int bar = intField(entry, "bar") - 1;
        const double beat = numberField(entry, "beat", 1) - 1;
        const wxString mark = Utf8Field(entry, "mark");
        if (bar < 0 || bar >= probe.settings.bars || beat < 0 || beat >= probe.BarBeats(bar)) {
            problems += wxString::Format("Dynamic at bar %d is out of range.\n", bar + 1);
            continue;
        }
        if (mark != "none" && mark != "pp" && mark != "p" && mark != "mp" && mark != "mf" && mark != "f" && mark != "ff") {
            problems += "\"" + mark + "\" is not a dynamic.\n";
            continue;
        }
        for (int staff : staves) {
            Marking m{bar, staff, beat, mark};
            (mark == "none" ? out.removedDynamics : out.dynamics).push_back(std::move(m));
        }
    }

    for (const json& entry : hairpins) {
        const std::vector<int> staves = stavesOf(entry);
        Hairpin h;
        h.bar = intField(entry, "start_bar") - 1;
        h.beat = numberField(entry, "start_beat", 1) - 1;
        h.endBar = intField(entry, "end_bar") - 1;
        h.endBeat = numberField(entry, "end_beat", 1) - 1;
        const wxString type = Utf8Field(entry, "type");
        const int bars = probe.settings.bars;
        if (h.bar < 0 || h.bar >= bars || h.beat < 0 || h.beat >= probe.BarBeats(h.bar)) {
            problems += wxString::Format("Hairpin at bar %d is out of range.\n", h.bar + 1);
            continue;
        }
        if (type != "none" && type != "crescendo" && type != "diminuendo") {
            problems += "\"" + type + "\" is not a hairpin type.\n";
            continue;
        }
        if (type != "none" && (h.endBar < 0 || h.endBar >= bars || h.endBeat < 0 || h.endBeat > probe.BarBeats(h.endBar) + 1e-6 ||
                               probe.BarStart(h.endBar) + h.endBeat <= probe.BarStart(h.bar) + h.beat)) {
            problems += wxString::Format("Hairpin at bar %d must end after it starts, within the score.\n", h.bar + 1);
            continue;
        }
        h.crescendo = type == "crescendo";
        for (int staff : staves) {
            h.staff = staff;
            (type == "none" ? out.removedHairpins : out.hairpins).push_back(h);
        }
    }

    if (!problems.empty()) {
        error = "The proposal was not shown. Fix these and call propose_edit again with everything:\n" + problems;
        return false;
    }

    if (!out.edits.empty())
        note(wxString::Format("Notes: %zu staves, bars %d-%d", touchedStaves.size(), minBar + 1, maxBar + 1));
    if (!out.dynamics.empty() || !out.removedDynamics.empty() || !out.hairpins.empty() || !out.removedHairpins.empty())
        note("Dynamics and hairpins");
    out.details = details;
    out.meterKey = MeterKey(project);
    return true;
}
