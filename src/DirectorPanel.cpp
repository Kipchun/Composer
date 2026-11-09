#include "DirectorPanel.h"

#include <array>

using namespace ui;

// Scrolling message list, including the actionable proposal card.
class ChatView : public wxScrolledCanvas {
public:
    enum class CardState { Pending, Previewing, Applied, Discarded };

    explicit ChatView(wxWindow* parent)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxVSCROLL | wxBORDER_NONE),
          dots_(this) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        Bind(wxEVT_PAINT, &ChatView::OnPaint, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Relayout(); e.Skip(); });
        Bind(wxEVT_LEFT_DOWN, &ChatView::OnLeftDown, this);
        Bind(wxEVT_MOTION, &ChatView::OnMotion, this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { ++dotPhase_; Refresh(); });
    }

    void Add(bool user, const wxString& text, bool card = false) {
        Message m;
        m.user = user;
        m.text = text;
        m.card = card;
        messages_.push_back(std::move(m));
        Relayout();
        ScrollToEnd();
    }

    void Clear() {
        messages_.clear();
        cardState_ = CardState::Pending;
        SetThinking(false);
        Scroll(0, 0);
    }

    void SetThinking(bool thinking) {
        thinking_ = thinking;
        if (thinking) dots_.Start(280);
        else dots_.Stop();
        Relayout();
        ScrollToEnd();
    }

    std::function<void(ProposalState)> onProposal;

private:
    struct Message {
        bool user = false;
        wxString text;
        bool card = false;
        std::vector<wxString> lines;
        double y = 0, height = 0, bubbleW = 0;
    };

    static constexpr double kPad = 14, kLineH = 21, kCardH = 158;

    wxFont BodyFont() const { return UiFont(14); }

    std::vector<wxString> Wrap(wxGraphicsContext& gc, const wxString& text, double maxWidth) {
        std::vector<wxString> lines;
        wxString line;
        for (const wxString& word : wxSplit(text, ' ')) {
            const wxString candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && TextWidth(gc, candidate, BodyFont()) > maxWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        if (!line.empty()) lines.push_back(line);
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
            m.lines = Wrap(*gc, m.text, maxText);
            m.bubbleW = 0;
            for (const wxString& line : m.lines)
                m.bubbleW = std::max(m.bubbleW, TextWidth(*gc, line, BodyFont()));
            double h = m.lines.size() * kLineH;
            h += m.user ? 18 : 24;  // bubble padding, or the sender label
            if (m.card) h += kCardH + 10;
            m.height = h;
            y += h + 14;
        }
        if (thinking_) y += 40;
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
        return wxRect2DDouble(kPad, top, GetClientSize().x - 2 * kPad, kCardH);
    }

    std::array<wxRect2DDouble, 3> CardButtons(const Message& m) const {
        const wxRect2DDouble card = CardRect(m);
        const double y = card.m_y + card.m_height - 42;
        const double x = card.m_x + 12;
        return {wxRect2DDouble(x, y, 104, 30), wxRect2DDouble(x + 110, y, 74, 30),
                wxRect2DDouble(x + 190, y, 76, 30)};
    }

    void DrawCard(wxGraphicsContext& gc, const Message& m) {
        const wxRect2DDouble card = CardRect(m);
        FillRound(gc, card, 7, kPanelAlt);
        StrokeRound(gc, card, 7, cardState_ == CardState::Discarded ? kBorder : kProposal, 1.2);

        const double x = card.m_x + 12;
        DrawTextAt(gc, "PROPOSED CHANGE", x, card.m_y + 10, UiFont(11, true), kProposalBright);
        DrawTextAt(gc, "Lift Violin I in bars 5" + Glyph(0x2013) + "6", x, card.m_y + 27,
                   UiFont(15, true), kText);
        DrawTextAt(gc, Glyph(0x2022) + "  Melody rises toward G5", x, card.m_y + 54, UiFont(13),
                   kMuted);
        DrawTextAt(gc, Glyph(0x2022) + "  Adds eighth-note motion to bar 5", x, card.m_y + 74,
                   UiFont(13), kMuted);

        if (cardState_ == CardState::Applied || cardState_ == CardState::Discarded) {
            const bool applied = cardState_ == CardState::Applied;
            DrawTextAt(gc, applied ? "Applied to the score" : "Discarded", x, card.m_y + 122,
                       UiFont(14, true), applied ? kProposalBright : kMuted);
            return;
        }

        const auto buttons = CardButtons(m);
        const bool previewing = cardState_ == CardState::Previewing;
        FillRound(gc, buttons[0], 5, previewing ? kProposal : kHover);
        DrawCentered(gc, previewing ? "Hide preview" : "Preview", buttons[0], UiFont(13, true),
                     *wxWHITE);
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

        if (thinking_) {
            const double y = contentH_ - 40;
            DrawTextAt(*gc, "Director is thinking", kPad, y, UiFont(13), kMuted);
            for (int i = 0; i < 3; ++i) {
                gc->SetBrush(wxBrush(dotPhase_ % 3 == i ? kAccent : kBorder));
                NoPen(*gc);
                gc->DrawEllipse(kPad + 132 + i * 11, y + 7, 7, 7);
            }
        }
    }

    int CardButtonAt(const wxPoint& client, const Message*& out) {
        const wxPoint p = CalcUnscrolledPosition(client);
        if (cardState_ == CardState::Applied || cardState_ == CardState::Discarded) return -1;
        for (const Message& m : messages_) {
            if (!m.card) continue;
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
        const Message* m = nullptr;
        SetCursor(CardButtonAt(e.GetPosition(), m) >= 0 ? wxCURSOR_HAND : wxCURSOR_ARROW);
    }

    void OnLeftDown(wxMouseEvent& e) {
        const Message* m = nullptr;
        const int button = CardButtonAt(e.GetPosition(), m);
        if (button < 0) return;
        ProposalState state = ProposalState::None;
        if (button == 0) {
            const bool on = cardState_ != CardState::Previewing;
            cardState_ = on ? CardState::Previewing : CardState::Pending;
            state = on ? ProposalState::Preview : ProposalState::None;
        } else if (button == 1) {
            cardState_ = CardState::Applied;
            state = ProposalState::Applied;
        } else {
            cardState_ = CardState::Discarded;
        }
        SetCursor(wxCURSOR_ARROW);
        Refresh();
        if (onProposal) onProposal(state);
    }

    std::vector<Message> messages_;
    CardState cardState_ = CardState::Pending;
    wxTimer dots_;
    int dotPhase_ = 0, contentH_ = 0;
    bool thinking_ = false;
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
            if (hit >= 0 && onChip) onChip(kChips[hit]);
        });
    }

    void SetScope(const wxString& scope) {
        scope_ = scope;
        Refresh();
    }

    std::function<void(const wxString&)> onChip;

private:
    static constexpr int kChipCount = 4;
    inline static const wxString kChips[kChipCount] = {"Warmer", "Countermelody", "Simplify piano",
                                                       "Explain bar"};

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

namespace {

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
            DrawTextAt(*gc, "Concept preview: replies are simulated", 60, 33, UiFont(12), kMuted);
            Line(*gc, 0, size.y - 0.5, size.x, size.y - 0.5, kBorder);
        });
    }
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
            FillRound(*gc, wxRect2DDouble(0, 0, 36, 36), 7, kAccent);
            wxGraphicsPath arrow = gc->CreatePath();
            arrow.MoveToPoint(18, 9);
            arrow.AddLineToPoint(26, 18);
            arrow.AddLineToPoint(20.5, 18);
            arrow.AddLineToPoint(20.5, 27);
            arrow.AddLineToPoint(15.5, 27);
            arrow.AddLineToPoint(15.5, 18);
            arrow.AddLineToPoint(10, 18);
            arrow.CloseSubpath();
            NoPen(*gc);
            gc->SetBrush(wxBrush(kAccentInk));
            gc->FillPath(arrow);
        });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (onClick) onClick();
        });
    }

    std::function<void()> onClick;
};

}  // namespace

DirectorPanel::DirectorPanel(wxWindow* parent) : wxPanel(parent), replyTimer_(this) {
    SetBackgroundColour(kPanel);

    auto* header = new DirectorHeader(this);
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
    auto* send = new SendButton(inputRow);

    auto* rowSizer = new wxBoxSizer(wxHORIZONTAL);
    rowSizer->Add(input_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    rowSizer->Add(send, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    inputRow->SetSizer(rowSizer);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(header, 0, wxEXPAND);
    sizer->Add(chat_, 1, wxEXPAND);
    sizer->Add(footer_, 0, wxEXPAND);
    sizer->Add(inputRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizer(sizer);

    chat_->Add(true, "The second phrase feels static. Can the violins lift it a little?");
    chat_->Add(false,
               "Agreed. Violin I stays in the same register through bars 5" + Glyph(0x2013) +
                   "6. I would carry it upward and add motion, leaving the harmony as written.",
               true);
    chat_->onProposal = [this](ProposalState state) {
        if (onProposal) onProposal(state);
    };

    const auto submit = [this] {
        Send(input_->GetValue());
        input_->Clear();
    };
    input_->Bind(wxEVT_TEXT_ENTER, [submit](wxCommandEvent&) { submit(); });
    send->onClick = submit;
    footer_->onChip = [this](const wxString& chip) { Send(chip); };

    replyTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Reply(); });
}

void DirectorPanel::SetScope(int bar, const wxString& staffName) {
    if (bar < 0) {
        scope_ = "Entire score";
    } else {
        scope_ = wxString::Format("Bar %d", bar + 1);
        if (!staffName.empty()) scope_ += " " + Glyph(0x00B7) + " " + staffName;
    }
    footer_->SetScope(scope_);
}

void DirectorPanel::ResetConversation(const wxString& projectTitle) {
    replyTimer_.Stop();
    chat_->Clear();
    chat_->Add(false, "Ready to work on " + projectTitle + ". Tell me what you'd like to change.");
    SetScope(-1, "");
}

void DirectorPanel::Send(const wxString& text) {
    const wxString trimmed = wxString(text).Trim().Trim(false);
    if (trimmed.empty() || replyTimer_.IsRunning()) return;
    pendingPrompt_ = trimmed;
    chat_->Add(true, trimmed);
    chat_->SetThinking(true);
    replyTimer_.StartOnce(1100);
}

void DirectorPanel::Reply() {
    chat_->SetThinking(false);
    chat_->Add(false, wxString::Format(
                          "This is a placeholder reply. Once connected, I would apply \"%s\" to: %s.",
                          pendingPrompt_, scope_));
}
