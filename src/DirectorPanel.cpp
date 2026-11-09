#include "DirectorPanel.h"

#include <algorithm>
#include <array>

#include "director/DirectorClient.h"
#include "director/DirectorSession.h"
#include "director/DirectorSettings.h"
#include "director/ScoreText.h"

using namespace ui;

// Scrolling message list, including the actionable proposal cards.
class ChatView : public wxScrolledCanvas {
public:
    enum class CardState { Pending, Previewing, Applied, Discarded, Superseded };

    explicit ChatView(wxWindow* parent)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE),
          dots_(this),
          stream_(this) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        Bind(wxEVT_PAINT, &ChatView::OnPaint, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Relayout(); e.Skip(); });
        Bind(wxEVT_LEFT_DOWN, &ChatView::OnLeftDown, this);
        Bind(wxEVT_MOTION, &ChatView::OnMotion, this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent& e) {
            if (e.GetId() == stream_.GetId()) {
                Relayout();
                ScrollToEnd();
            } else {
                ++dotPhase_;
                Refresh();
            }
        });
    }

    void Add(bool user, const wxString& text) {
        Message m;
        m.user = user;
        m.text = text;
        messages_.push_back(std::move(m));
        Relayout();
        ScrollToEnd();
    }

    // Appends streamed text, redrawing at a steady rate rather than per fragment.
    void AppendReply(const wxString& text) {
        if (messages_.empty() || messages_.back().user || messages_.back().card || !streaming_) {
            messages_.emplace_back();
            streaming_ = true;
        }
        messages_.back().text += text;
        if (!stream_.IsRunning()) stream_.StartOnce(50);
    }

    void EndReply() {
        streaming_ = false;
        stream_.Stop();
        Relayout();
        ScrollToEnd();
    }

    void AddCard(const Proposal& proposal) {
        // Only the newest proposal stays actionable.
        for (Message& m : messages_) {
            if (m.card && (m.cardState == CardState::Pending || m.cardState == CardState::Previewing)) {
                if (m.cardState == CardState::Previewing && onProposal) onProposal(nullptr, ProposalState::None);
                m.cardState = CardState::Superseded;
            }
        }
        if (messages_.empty() || messages_.back().user || messages_.back().card) messages_.emplace_back();
        Message& m = messages_.back();
        m.card = true;
        m.proposal = proposal;
        streaming_ = false;
        Relayout();
        ScrollToEnd();
    }

    void Clear() {
        messages_.clear();
        streaming_ = false;
        SetThinking(wxEmptyString);
        Scroll(0, 0);
    }

    // An empty label hides the indicator.
    void SetThinking(const wxString& label) {
        thinking_ = label;
        if (!label.empty()) {
            if (!dots_.IsRunning()) dots_.Start(280);
        } else {
            dots_.Stop();
        }
        Relayout();
        ScrollToEnd();
    }

    void CheckStale(const ScoreCanvas* score, bool invalidateAll) {
        bool changed = false;
        for (Message& m : messages_) {
            if (!m.card || (m.cardState != CardState::Pending && m.cardState != CardState::Previewing)) continue;
            const bool stale = invalidateAll || m.stale || score->IsProposalStale(m.proposal);
            if (stale == m.stale) continue;
            m.stale = stale;
            changed = true;
            if (stale && m.cardState == CardState::Previewing) {
                m.cardState = CardState::Pending;
                if (onProposal) onProposal(nullptr, ProposalState::None);
            }
        }
        if (changed) Refresh();
    }

    // The score left the preview on its own, so the card just goes back to waiting.
    void PreviewEnded() {
        for (Message& m : messages_)
            if (m.card && m.cardState == CardState::Previewing) m.cardState = CardState::Pending;
        Refresh();
    }

    // Tells the model what happened to its proposals since it last spoke.
    wxString ProposalOutcomes() {
        wxString out;
        for (Message& m : messages_) {
            if (!m.card || m.reported) continue;
            if (m.cardState == CardState::Applied) out += "The user applied: " + m.proposal.title + "\n";
            else if (m.cardState == CardState::Discarded) out += "The user discarded: " + m.proposal.title + "\n";
            else continue;
            m.reported = true;
        }
        return out;
    }

    std::function<void(const Proposal*, ProposalState)> onProposal;
    const ScoreCanvas* score = nullptr;

private:
    struct Message {
        bool user = false;
        wxString text;
        std::vector<wxString> lines;
        double y = 0, height = 0, bubbleW = 0;

        bool card = false, stale = false, reported = false;
        Proposal proposal;
        CardState cardState = CardState::Pending;
        std::vector<wxString> summaryLines;
        double cardH = 0;
    };

    static constexpr double kPad = 14, kLineH = 21, kSummaryH = 19;

    wxFont BodyFont() const { return UiFont(14); }

    std::vector<wxString> Wrap(wxGraphicsContext& gc, const wxString& text, double maxWidth, const wxFont& font) {
        std::vector<wxString> lines;
        for (const wxString& paragraph : wxSplit(text, '\n', 0)) {
            wxString line;
            for (const wxString& word : wxSplit(paragraph, ' ', 0)) {
                const wxString candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && TextWidth(gc, candidate, font) > maxWidth) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = candidate;
                }
            }
            lines.push_back(line);
        }
        return lines;
    }

    void Relayout() {
        const double width = GetClientSize().x;
        if (width <= 0) return;
        Gc gc(wxGraphicsContext::Create(this));
        double y = 14;
        for (Message& m : messages_) {
            m.y = y;
            const double maxText = m.user ? width * 0.78 - 26 : width - 2 * kPad;
            m.lines.clear();
            if (!m.text.empty()) m.lines = Wrap(*gc, m.text, maxText, BodyFont());
            m.bubbleW = 0;
            for (const wxString& line : m.lines) m.bubbleW = std::max(m.bubbleW, TextWidth(*gc, line, BodyFont()));
            double h = m.lines.size() * kLineH;
            h += m.user ? 18 : 24;  // bubble padding, or the sender label
            if (m.card) {
                const wxString body = m.proposal.details.empty() ? m.proposal.summary
                                                                 : m.proposal.summary + "\n" + m.proposal.details;
                m.summaryLines = Wrap(*gc, body, width - 2 * kPad - 24, UiFont(13));
                m.cardH = 58 + m.summaryLines.size() * kSummaryH + 52;
                h += m.cardH + 10;
            }
            m.height = h;
            y += h + 14;
        }
        if (!thinking_.empty()) y += 40;
        contentH_ = int(y);
        SetVirtualSize(int(width), contentH_);
        Refresh();
    }

    void ScrollToEnd() {
        int ppuX, ppuY;
        GetScrollPixelsPerUnit(&ppuX, &ppuY);
        if (ppuY > 0) Scroll(0, (contentH_ - GetClientSize().y) / ppuY + 1);
    }

    wxRect2DDouble CardRect(const Message& m) const {
        const double top = m.y + 22 + m.lines.size() * kLineH + 8;
        return wxRect2DDouble(kPad, top, GetClientSize().x - 2 * kPad, m.cardH);
    }

    std::array<wxRect2DDouble, 3> CardButtons(const Message& m) const {
        const wxRect2DDouble card = CardRect(m);
        const double y = card.m_y + card.m_height - 42;
        const double x = card.m_x + 12;
        return {wxRect2DDouble(x, y, 104, 30), wxRect2DDouble(x + 110, y, 74, 30),
                wxRect2DDouble(x + 190, y, 76, 30)};
    }

    static bool IsOpen(const Message& m) {
        return m.cardState == CardState::Pending || m.cardState == CardState::Previewing;
    }

    void DrawCard(wxGraphicsContext& gc, const Message& m) {
        const wxRect2DDouble card = CardRect(m);
        FillRound(gc, card, 7, kPanelAlt);
        StrokeRound(gc, card, 7, IsOpen(m) && !m.stale ? kProposal : kBorder, 1.2);

        const double x = card.m_x + 12;
        DrawTextAt(gc, "PROPOSED CHANGE", x, card.m_y + 10, UiFont(11, true), kProposalBright);
        DrawTextAt(gc, m.proposal.title, x, card.m_y + 27, UiFont(15, true), kText);
        for (size_t i = 0; i < m.summaryLines.size(); ++i)
            DrawTextAt(gc, m.summaryLines[i], x, card.m_y + 54 + i * kSummaryH, UiFont(13), kMuted);

        const auto buttons = CardButtons(m);
        const double statusY = buttons[0].m_y + 6;
        if (!IsOpen(m)) {
            const bool applied = m.cardState == CardState::Applied;
            const wxString text = applied ? "Applied to the score"
                                  : m.cardState == CardState::Discarded ? "Discarded"
                                                                        : "Replaced by a newer proposal";
            DrawTextAt(gc, text, x, statusY, UiFont(14, true), applied ? kProposalBright : kMuted);
            return;
        }
        if (m.stale) {
            DrawTextAt(gc, "Score changed, so this no longer applies", x, statusY, UiFont(14, true), kMuted);
            return;
        }

        const bool previewing = m.cardState == CardState::Previewing;
        FillRound(gc, buttons[0], 5, previewing ? kProposal : kHover);
        DrawCentered(gc, previewing ? "Hide preview" : "Preview", buttons[0], UiFont(13, true), *wxWHITE);
        FillRound(gc, buttons[1], 5, kAccent);
        DrawCentered(gc, "Apply", buttons[1], UiFont(13, true), kAccentInk);
        StrokeRound(gc, buttons[2], 5, kBorder, 1);
        DrawCentered(gc, "Discard", buttons[2], UiFont(13), kText);
    }

    void OnPaint(wxPaintEvent&) {
        wxBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(kPanel));
        dc.Clear();
        Gc gc = MakeGc(dc);
        const wxPoint origin = CalcScrolledPosition(wxPoint(0, 0));
        gc->Translate(origin.x, origin.y);

        const double width = GetClientSize().x;
        for (const Message& m : messages_) {
            if (m.user) {
                const double bubbleW = m.bubbleW + 26;
                const wxRect2DDouble r(width - kPad - bubbleW, m.y, bubbleW, m.height);
                FillRound(*gc, r, 9, kHover);
                for (size_t i = 0; i < m.lines.size(); ++i)
                    DrawTextAt(*gc, m.lines[i], r.m_x + 13, m.y + 8 + i * kLineH, BodyFont(), kText);
            } else {
                NoPen(*gc);
                gc->SetBrush(wxBrush(kAccent));
                gc->DrawEllipse(kPad, m.y + 5, 8, 8);
                DrawTextAt(*gc, "Director", kPad + 14, m.y, UiFont(12, true), kAccent);
                for (size_t i = 0; i < m.lines.size(); ++i)
                    DrawTextAt(*gc, m.lines[i], kPad, m.y + 20 + i * kLineH, BodyFont(), kText);
                if (m.card) DrawCard(*gc, m);
            }
        }

        if (!thinking_.empty()) {
            const double y = contentH_ - 40;
            const wxFont font = UiFont(13);
            DrawTextAt(*gc, thinking_, kPad, y, font, kMuted);
            const double dotsX = kPad + TextWidth(*gc, thinking_, font) + 10;
            for (int i = 0; i < 3; ++i) {
                gc->SetBrush(wxBrush(dotPhase_ % 3 == i ? kAccent : kBorder));
                NoPen(*gc);
                gc->DrawEllipse(dotsX + i * 11, y + 7, 7, 7);
            }
        }
    }

    int CardButtonAt(const wxPoint& client, Message*& out) {
        const wxPoint p = CalcUnscrolledPosition(client);
        for (Message& m : messages_) {
            if (!m.card || !IsOpen(m) || m.stale) continue;
            const auto buttons = CardButtons(m);
            for (int i = 0; i < 3; ++i) {
                if (Hit(buttons[i], p)) {
                    out = &m;
                    return i;
                }
            }
        }
        return -1;
    }

    void OnMotion(wxMouseEvent& e) {
        Message* m = nullptr;
        SetCursor(CardButtonAt(e.GetPosition(), m) >= 0 ? wxCURSOR_HAND : wxCURSOR_ARROW);
    }

    void OnLeftDown(wxMouseEvent& e) {
        Message* m = nullptr;
        const int button = CardButtonAt(e.GetPosition(), m);
        if (button < 0) return;

        // The score may have moved on since the card last checked.
        if (button != 2 && score && score->IsProposalStale(m->proposal)) {
            m->stale = true;
            Refresh();
            return;
        }

        const Proposal* proposal = &m->proposal;
        ProposalState state = ProposalState::None;
        if (button == 0) {
            const bool on = m->cardState != CardState::Previewing;
            m->cardState = on ? CardState::Previewing : CardState::Pending;
            state = on ? ProposalState::Preview : ProposalState::None;
        } else if (button == 1) {
            m->cardState = CardState::Applied;
            state = ProposalState::Applied;
        } else {
            m->cardState = CardState::Discarded;
        }
        SetCursor(wxCURSOR_ARROW);
        Refresh();
        if (onProposal) onProposal(state == ProposalState::None ? nullptr : proposal, state);
    }

    std::vector<Message> messages_;
    wxTimer dots_, stream_;
    int dotPhase_ = 0, contentH_ = 0;
    wxString thinking_;
    bool streaming_ = false;
};

// Shows what the Director is working on, plus one-click prompt suggestions.
class DirectorFooter : public wxPanel {
public:
    explicit DirectorFooter(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 68)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &DirectorFooter::OnPaint, this);
        Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
            int hit = ChipAt(e.GetPosition());
            if (hit != hover_) { hover_ = hit; SetCursor(hit >= 0 ? wxCURSOR_HAND : wxCURSOR_ARROW); Refresh(); }
        });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover_ = -1; Refresh(); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
            const int hit = ChipAt(e.GetPosition());
            if (hit >= 0 && onChip) onChip(kPrompts[hit]);
        });
    }

    void SetScope(const wxString& scope) {
        scope_ = scope;
        Refresh();
    }

    std::function<void(const wxString&)> onChip;

private:
    static constexpr int kChipCount = 4;
    inline static const wxString kChips[kChipCount] = {"Warmer", "Countermelody", "Simplify piano", "Explain bar"};
    inline static const wxString kPrompts[kChipCount] = {
        "Make this passage sound warmer.", "Add a countermelody here.",
        "Simplify the piano part here.", "Explain what's happening harmonically here."};

    int ChipAt(const wxPoint& p) const {
        for (int i = 0; i < kChipCount; ++i)
            if (Hit(rects_[i], p)) return i;
        return -1;
    }

    void OnPaint(wxPaintEvent&) {
        wxBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(kPanel));
        dc.Clear();
        Gc gc = MakeGc(dc);
        Line(*gc, 0, 0.5, GetClientSize().x, 0.5, kBorder);

        const wxFont label = UiFont(12);
        DrawTextAt(*gc, "Working on", 14, 9, label, kMuted);
        const double textW = TextWidth(*gc, scope_, UiFont(12, true));
        const wxRect2DDouble scope(84, 7, textW + 18, 21);
        FillRound(*gc, scope, 10, WithAlpha(kSelect, 70));
        DrawCentered(*gc, scope_, scope, UiFont(12, true), kText);

        double x = 14;
        const wxFont font = UiFont(12);
        for (int i = 0; i < kChipCount; ++i) {
            const double w = TextWidth(*gc, kChips[i], font) + 22;
            rects_[i] = wxRect2DDouble(x, 36, w, 25);
            FillRound(*gc, rects_[i], 12, i == hover_ ? kHover : kPanelAlt);
            DrawCentered(*gc, kChips[i], rects_[i], font, kText);
            x += w + 6;
        }
    }

    wxRect2DDouble rects_[kChipCount];
    wxString scope_ = "Entire score";
    int hover_ = -1;
};

class DirectorHeader : public wxPanel {
public:
    explicit DirectorHeader(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 58)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(kPanel));
            dc.Clear();
            Gc gc = MakeGc(dc);
            const wxSize size = GetClientSize();

            NoPen(*gc);
            gc->SetBrush(wxBrush(kAccent));
            gc->DrawEllipse(14, 11, 36, 36);
            SetStroke(*gc, kAccentInk, 2.6);
            gc->StrokeLine(24, 38, 41, 20);
            NoPen(*gc);
            gc->SetBrush(wxBrush(kAccentInk));
            gc->DrawEllipse(21, 35, 7, 7);

            DrawTextAt(*gc, "Director", 60, 9, UiFont(17, true), kText);
            DrawTextAt(*gc, subtitle_, 60, 33, UiFont(12), kMuted);
            Line(*gc, 0, size.y - 0.5, size.x, size.y - 0.5, kBorder);
        });
    }

    void SetSubtitle(const wxString& text) {
        subtitle_ = text;
        Refresh();
    }

private:
    wxString subtitle_;
};

class SendButton : public wxPanel {
public:
    explicit SendButton(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(36, 36)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCURSOR_HAND);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            Gc gc = MakeGc(dc);
            FillRound(*gc, wxRect2DDouble(0, 0, 36, 36), 7, stop_ ? kHover : kAccent);
            NoPen(*gc);
            if (stop_) {
                gc->SetBrush(wxBrush(kText));
                gc->DrawRectangle(12, 12, 12, 12);
                return;
            }
            wxGraphicsPath arrow = gc->CreatePath();
            arrow.MoveToPoint(18, 9);
            arrow.AddLineToPoint(26, 18);
            arrow.AddLineToPoint(20.5, 18);
            arrow.AddLineToPoint(20.5, 27);
            arrow.AddLineToPoint(15.5, 27);
            arrow.AddLineToPoint(15.5, 18);
            arrow.AddLineToPoint(10, 18);
            arrow.CloseSubpath();
            gc->SetBrush(wxBrush(kAccentInk));
            gc->FillPath(arrow);
        });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (onClick) onClick();
        });
    }

    void SetStopMode(bool stop) {
        stop_ = stop;
        SetToolTip(stop ? "Stop" : "Send");
        Refresh();
    }
    bool IsStopMode() const { return stop_; }

    std::function<void()> onClick;

private:
    bool stop_ = false;
};

DirectorPanel::DirectorPanel(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundColour(kPanel);

    header_ = new DirectorHeader(this);
    chat_ = new ChatView(this);
    footer_ = new DirectorFooter(this);

    auto* inputRow = new wxPanel(this);
    inputRow->SetBackgroundColour(kPanelAlt);
    input_ = new wxTextCtrl(inputRow, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                            wxTE_PROCESS_ENTER | wxBORDER_NONE);
    input_->SetBackgroundColour(kPanelAlt);
    input_->SetForegroundColour(kText);
    input_->SetFont(UiFont(14));
    input_->SetHint("Tell the Director what to change...");
    send_ = new SendButton(inputRow);

    auto* rowSizer = new wxBoxSizer(wxHORIZONTAL);
    rowSizer->Add(input_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    rowSizer->Add(send_, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    inputRow->SetSizer(rowSizer);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(header_, 0, wxEXPAND);
    sizer->Add(chat_, 1, wxEXPAND);
    sizer->Add(footer_, 0, wxEXPAND);
    sizer->Add(inputRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizer(sizer);

    session_ = std::make_unique<DirectorSession>([this]() -> const Project& { return score_->GetProject(); });
    session_->onText = [this](const wxString& text) {
        chat_->SetThinking(wxEmptyString);
        chat_->AppendReply(text);
    };
    session_->onStatus = [this](const wxString& status) { chat_->SetThinking(status); };
    session_->onProposal = [this](const Proposal& proposal) { chat_->AddCard(proposal); };
    session_->onCost = [this](double dollars) {
        cost_ = dollars;
        RefreshModelName();
    };
    session_->onFinished = [this](int status, const wxString& error) {
        chat_->SetThinking(wxEmptyString);
        chat_->EndReply();
        SetBusy(false);
        if (status == 401)
            chat_->Add(false, "Your API key was rejected. Check it in Director > Settings.");
        else if (!error.empty())
            chat_->Add(false, error);
    };

    chat_->onProposal = [this](const Proposal* proposal, ProposalState state) {
        if (onProposal) onProposal(proposal, state);
    };

    const auto submit = [this] {
        if (send_->IsStopMode()) {
            CancelRequest();
            chat_->Add(false, "Stopped.");
            return;
        }
        const wxString text = input_->GetValue();
        if (session_->IsBusy() || wxString(text).Trim().Trim(false).empty()) return;
        input_->Clear();
        Send(text);
    };
    input_->Bind(wxEVT_TEXT_ENTER, [submit](wxCommandEvent&) { submit(); });
    send_->onClick = submit;
    footer_->onChip = [this](const wxString& chip) { Send(chip); };
    RefreshModelName();
}

DirectorPanel::~DirectorPanel() = default;

void DirectorPanel::RefreshModelName() {
    wxString text = DirectorModelName(LoadDirectorModel());
    if (cost_ > 0) text += "  " + Glyph(0x00B7) + wxString::Format("  about $%.2f this session", cost_);
    header_->SetSubtitle(text);
}

void DirectorPanel::SetBusy(bool busy) { send_->SetStopMode(busy); }

void DirectorPanel::SetScope(const BarRange& range) {
    scopeRange_ = range;
    if (!range.IsValid()) {
        scope_ = "Entire score";
    } else {
        scope_ = range.firstBar == range.lastBar ? wxString::Format("Bar %d", range.firstBar + 1)
                                                 : wxString::Format("Bars %d-%d", range.firstBar + 1, range.lastBar + 1);
        if (range.firstStaff >= 0 && score_) {
            scope_ += " " + Glyph(0x00B7) + " " + score_->StaffName(range.firstStaff);
            if (range.lastStaff != range.firstStaff) scope_ += " to " + score_->StaffName(range.lastStaff);
        }
    }
    footer_->SetScope(scope_);
}

void DirectorPanel::CancelRequest() {
    session_->Cancel();
    chat_->SetThinking(wxEmptyString);
    chat_->EndReply();
    SetBusy(false);
}

void DirectorPanel::ResetConversation(const wxString& projectTitle) {
    session_->Reset();
    cost_ = 0;
    RefreshModelName();
    SetBusy(false);
    chat_->Clear();
    chat_->Add(false, "Ready to work on " + projectTitle + ". Tell me what you'd like to change.");
    SetScope(BarRange{});
}

void DirectorPanel::ShowSampleConversation(const Proposal& proposal) {
    session_->Reset();
    SetBusy(false);
    chat_->Clear();
    chat_->score = score_;
    chat_->Add(true, "The second phrase feels static. Can the violins lift it a little?");
    chat_->AppendReply("Agreed. Violin I stays in the same register through bars 5" + Glyph(0x2013) +
                       "6. I would carry it upward and add motion, leaving the harmony as written.");
    chat_->EndReply();
    chat_->AddCard(proposal);
    SetScope(BarRange{});
}

void DirectorPanel::PreviewEnded() { chat_->PreviewEnded(); }

void DirectorPanel::ScoreChanged() { chat_->CheckStale(score_, false); }

void DirectorPanel::InvalidateProposals() { chat_->CheckStale(score_, true); }

wxString DirectorPanel::BuildContext() const {
    const Project& project = score_->GetProject();
    wxString context = "Current project:\n" + DescribeProject(project) + "\n\nSelection: " + scope_ + "\n";

    wxString outcomes = chat_->ProposalOutcomes();
    if (!outcomes.empty()) context += "\n" + outcomes;

    const int bars = project.settings.bars;
    int first, last;
    if (scopeRange_.IsValid()) {
        first = std::max(0, scopeRange_.firstBar - 2);
        last = std::min({bars - 1, scopeRange_.lastBar + 2, first + 24});
    } else {
        first = 0;
        last = std::min(bars - 1, 7);
    }
    context += wxString::Format("\nCurrent notes, bars %d-%d (resting staves left out):\n", first + 1, last + 1);
    context += BarsToText(project, first, last, {}, true);
    return context;
}

void DirectorPanel::Send(const wxString& text) {
    const wxString message = wxString(text).Trim().Trim(false);
    if (message.empty() || session_->IsBusy() || !score_) return;

    wxString key = LoadDirectorKey();
    if (key.empty() && !DirectorClient::IsMock()) {
        if (onOpenSettings) onOpenSettings();
        key = LoadDirectorKey();
        RefreshModelName();
        if (key.empty()) {
            chat_->Add(false, "Add your Claude API key in Director > Settings to start.");
            return;
        }
    }

    chat_->score = score_;
    chat_->Add(true, message);
    SetBusy(true);
    session_->Send(message, BuildContext(), key, LoadDirectorModel());
}
