// Sing a melody into the microphone, tidy it in a piano-roll pattern editor, then put it in
// the score or hand it to the Director to develop.
#pragma once

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <vector>

#include "Project.h"
#include "audio/AudioEngine.h"
#include "audio/MicRecorder.h"
#include "sing/Melody.h"

class PianoRoll;
class LevelBar;

class SingWindow : public wxFrame {
public:
    // What the window needs from the rest of the app.
    struct Host {
        std::function<const Project&()> project;
        std::function<std::vector<PlayNote>()> scorePlayback;
        std::function<std::vector<double>()> clickBeats;
        std::function<std::vector<std::pair<double, double>>()> tempoPoints;
        std::function<int()> selectedBar;      // -1 if nothing is selected
        std::function<void()> stopMainPlayback;
        std::function<void()> restoreSchedule;  // after a take or playback, put the score's schedule back
        std::function<void(const Proposal&)> insert;
        // Sends a message to the Director with extra context; false if it couldn't be sent.
        std::function<bool(const wxString& message, const wxString& context)> develop;
        std::function<void(bool recording)> recordingChanged;
    };

    SingWindow(wxWindow* parent, Host host, AudioEngine* engine);
    ~SingWindow() override;

    // The engine is about to be replaced: stop everything that uses it.
    void Detach();
    // A new or changed project is in place.
    void Attach(AudioEngine* engine);
    bool IsBusy() const { return mode_ != Mode::Idle; }
    void StopAll();
    void ShowFor(int bar);  // opens the window ready to record from a bar (-1 keeps the current one)

private:
    friend class PianoRoll;
    enum class Mode { Idle, Recording, Playing };

    void BuildUi();
    void RefreshStaves();
    void StartRecording();
    void StartPlayback();
    void Finish();
    void OnTick(wxTimerEvent&);
    void UpdateTake(bool final);
    void UpdateButtons();
    void SetStatus(const wxString& text);

    // Pattern edits, each undoable.
    void PushUndo();
    void Undo();
    void Redo();
    void Changed();  // after any edit
    void Transpose(int semitones);
    void FitOctaveToStaff();  // moves every note by octaves to sit well on the chosen staff
    void Requantize();

    bool BuildBars(int& first, int& last, std::vector<Bar>& bars, std::vector<wxString>& texts, wxString& error) const;
    void InsertIntoScore();
    void DevelopWithDirector();

    double Grid() const;
    int Staff() const;
    int Track() const;
    const Project& GetProject() const { return host_.project(); }

    Host host_;
    AudioEngine* engine_;
    MicRecorder mic_;
    std::unique_ptr<PitchTracker> tracker_;
    size_t micRead_ = 0;
    Mode mode_ = Mode::Idle;
    wxTimer timer_;
    bool silentWarned_ = false;

    // The current take.
    double takeStartBeat_ = 0;
    double offsetSeconds_ = 0;
    bool haveOffset_ = false;
    std::vector<SungNote> sung_;
    std::vector<PatternNote> keptOutside_;  // notes outside the take, kept when it's recorded over
    double takeEndBeat_ = 0;
    std::vector<PatternNote> beforeTake_;
    size_t takeCount_ = 0;
    int takeOctaves_ = 0;
    std::vector<std::pair<double, float>> trace_;  // (beat, sung midi) for drawing
    size_t traceDone_ = 0;
    double tuningCents_ = 0;

    std::vector<PatternNote> notes_;
    std::vector<std::vector<PatternNote>> undo_, redo_;
    bool edited_ = false;  // edited by hand since the last take, so changing the grid snaps rather than redoes
    int shift_ = 0;        // semitones applied to the take, so the trace lines up with the notes

    PianoRoll* roll_ = nullptr;
    LevelBar* level_ = nullptr;
    wxButton *record_ = nullptr, *play_ = nullptr, *stop_ = nullptr;
    wxChoice *staff_ = nullptr, *countIn_ = nullptr, *grid_ = nullptr;
    wxSpinCtrl* fromBar_ = nullptr;
    wxCheckBox *click_ = nullptr, *hearScore_ = nullptr, *keepKey_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxTextCtrl* ideas_ = nullptr;
    wxButton *insert_ = nullptr, *develop_ = nullptr;
    wxString staffName_;
};
