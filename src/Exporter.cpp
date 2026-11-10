#include "Exporter.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <wx/checklst.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/filepicker.h>
#include <wx/mstream.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/zstream.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "ScoreCanvas.h"
#include "Theme.h"
#include "audio/AudioEngine.h"

using Microsoft::WRL::ComPtr;
using namespace ui;

namespace {

constexpr int kPagePixels = 2550;  // 8.5 inches at 300 dpi
constexpr double kPagePoints = 612;

enum class Kind { Pdf, Wav, Mp3 };

struct Job {
    wxString path;
    Kind kind;
    int track;  // -1 for the full score
};

wxString SafeName(wxString text) {
    for (const wxUniChar c : wxString("\\/:*?\"<>|")) text.Replace(wxString(c), "-");
    text.Trim().Trim(false);
    return text.empty() ? wxString("Untitled") : text;
}

std::vector<Job> BuildJobs(const Project& project, const ExportOptions& o) {
    std::vector<Job> jobs;
    const wxString base = SafeName(project.settings.title);
    const auto add = [&](const wxString& stem, int track) {
        const wxString path = o.folder + wxFILE_SEP_PATH + stem;
        if (o.pdf) jobs.push_back({path + ".pdf", Kind::Pdf, track});
        if (o.wav) jobs.push_back({path + ".wav", Kind::Wav, track});
        if (o.mp3) jobs.push_back({path + ".mp3", Kind::Mp3, track});
    };
    if (o.fullScore) add(base + " - Full Score", -1);
    for (int track : o.partTracks) add(base + " - " + SafeName(project.tracks[track].name), track);
    return jobs;
}

// ---------------------------------------------------------------------------
// PDF: one full-page grayscale image per page.

class PdfWriter {
public:
    explicit PdfWriter(const wxString& path) : file_(path, "wb") { Write("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n"); }
    bool IsOk() const { return file_.IsOpened() && ok_; }

    bool AddPage(const wxImage& image) {
        const int w = image.GetWidth(), h = image.GetHeight();
        wxMemoryOutputStream compressed;
        {
            wxZlibOutputStream zlib(compressed, 6, wxZLIB_ZLIB);
            const unsigned char* rgb = image.GetData();
            std::vector<unsigned char> row(w);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x, rgb += 3) row[x] = (rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8;
                zlib.Write(row.data(), row.size());
            }
        }

        const double heightPoints = kPagePoints * h / w;
        const int imageId = NextId(), contentId = NextId(), pageId = NextId();

        BeginObject(imageId);
        Write(wxString::Format("<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray "
                               "/BitsPerComponent 8 /Filter /FlateDecode /Length %llu >>\nstream\n",
                               w, h, (unsigned long long)compressed.GetSize()).ToStdString());
        const wxStreamBuffer* buffer = compressed.GetOutputStreamBuffer();
        WriteBytes(buffer->GetBufferStart(), compressed.GetSize());
        Write("\nendstream\nendobj\n");

        const std::string content = wxString::FromCDouble(kPagePoints).ToStdString() + " 0 0 " +
                                    wxString::FromCDouble(heightPoints).ToStdString() + " 0 0 cm /Im0 Do";
        const std::string wrapped = "q " + content + " Q";
        BeginObject(contentId);
        Write(wxString::Format("<< /Length %zu >>\nstream\n", wrapped.size()).ToStdString());
        Write(wrapped);
        Write("\nendstream\nendobj\n");

        BeginObject(pageId);
        Write(wxString::Format("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %s %s] /Contents %d 0 R "
                               "/Resources << /XObject << /Im0 %d 0 R >> >> >>\nendobj\n",
                               wxString::FromCDouble(kPagePoints), wxString::FromCDouble(heightPoints), contentId, imageId)
                  .ToStdString());
        pages_.push_back(pageId);
        return ok_;
    }

    bool Finish() {
        BeginObject(1);
        Write("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
        BeginObject(2);
        std::string kids;
        for (int page : pages_) kids += std::to_string(page) + " 0 R ";
        Write("<< /Type /Pages /Count " + std::to_string(pages_.size()) + " /Kids [" + kids + "] >>\nendobj\n");

        const wxFileOffset xref = file_.Tell();
        const int count = nextId_;
        Write("xref\n0 " + std::to_string(count) + "\n0000000000 65535 f \n");
        offsets_.resize(count);
        for (int id = 1; id < count; ++id) Write(wxString::Format("%010lld 00000 n \n", (long long)offsets_[id]).ToStdString());
        Write("trailer\n<< /Size " + std::to_string(count) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n");
        return ok_ && file_.Close();
    }

private:
    int NextId() { return nextId_++; }
    void BeginObject(int id) {
        if (int(offsets_.size()) <= id) offsets_.resize(id + 1);
        offsets_[id] = file_.Tell();
        Write(std::to_string(id) + " 0 obj\n");
    }
    void Write(const std::string& text) { WriteBytes(text.data(), text.size()); }
    void WriteBytes(const void* data, size_t size) { ok_ = ok_ && file_.Write(data, size) == size; }

    wxFFile file_;
    bool ok_ = true;
    int nextId_ = 3;  // 1 and 2 are the catalog and page tree
    std::vector<wxFileOffset> offsets_;
    std::vector<int> pages_;
};

// ---------------------------------------------------------------------------
// Audio files.

bool WriteWav(const wxString& path, const std::vector<float>& samples, int sampleRate) {
    wxFFile file(path, "wb");
    if (!file.IsOpened()) return false;

    const uint32_t dataBytes = uint32_t(samples.size() * 3);
    const auto u32 = [&](uint32_t v) { file.Write(&v, 4); };
    const auto u16 = [&](uint16_t v) { file.Write(&v, 2); };
    file.Write("RIFF", 4);
    u32(36 + dataBytes);
    file.Write("WAVEfmt ", 8);
    u32(16);
    u16(1);  // PCM
    u16(2);
    u32(sampleRate);
    u32(sampleRate * 6);
    u16(6);
    u16(24);
    file.Write("data", 4);
    u32(dataBytes);

    std::vector<unsigned char> bytes(samples.size() * 3);
    for (size_t i = 0; i < samples.size(); ++i) {
        const int v = int(std::lround(std::clamp(samples[i], -1.f, 1.f) * 8388607.0f));
        bytes[i * 3] = v & 0xFF;
        bytes[i * 3 + 1] = (v >> 8) & 0xFF;
        bytes[i * 3 + 2] = (v >> 16) & 0xFF;
    }
    return file.Write(bytes.data(), bytes.size()) == bytes.size() && file.Close();
}

// MP3 only allows certain sample rates, so an unusual device rate is converted first.
std::vector<float> ToMp3Rate(const std::vector<float>& input, int rate, int& outRate) {
    if (rate == 44100 || rate == 48000) {
        outRate = rate;
        return input;
    }
    outRate = 48000;
    const double ratio = double(rate) / outRate;
    const size_t inFrames = input.size() / 2;
    const size_t outFrames = size_t(inFrames / ratio);
    std::vector<float> out(outFrames * 2);
    for (size_t i = 0; i < outFrames; ++i) {
        const double position = i * ratio;
        const size_t a = std::min(size_t(position), inFrames - 1), b = std::min(a + 1, inFrames - 1);
        const float t = float(position - a);
        out[i * 2] = input[a * 2] * (1 - t) + input[b * 2] * t;
        out[i * 2 + 1] = input[a * 2 + 1] * (1 - t) + input[b * 2 + 1] * t;
    }
    return out;
}

// Uses the MP3 encoder built into Windows.
bool WriteMp3(const wxString& path, const std::vector<float>& input, int inputRate, wxString* why) {
    int rate = 0;
    const std::vector<float> samples = ToMp3Rate(input, inputRate, rate);
    const auto fail = [&](const char* step, HRESULT hr) {
        if (why) *why = wxString::Format("%s (0x%08lX)", step, (unsigned long)hr);
        return false;
    };

    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return fail("Couldn't start Windows Media Foundation", hr);
    struct Shutdown { ~Shutdown() { MFShutdown(); } } shutdown;

    ComPtr<IMFAttributes> attributes;
    MFCreateAttributes(&attributes, 1);
    attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MP3);

    ComPtr<IMFSinkWriter> writer;
    hr = MFCreateSinkWriterFromURL(path.wc_str(), nullptr, attributes.Get(), &writer);
    if (FAILED(hr)) return fail("Couldn't create the MP3 file", hr);

    ComPtr<IMFMediaType> outType;
    MFCreateMediaType(&outType);
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
    outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 40000);  // 320 kbps
    DWORD stream = 0;
    hr = writer->AddStream(outType.Get(), &stream);
    if (FAILED(hr)) return fail("This system has no MP3 encoder", hr);

    ComPtr<IMFMediaType> inType;
    MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * 4);
    hr = writer->SetInputMediaType(stream, inType.Get(), nullptr);
    if (FAILED(hr)) return fail("The MP3 encoder rejected the audio format", hr);
    hr = writer->BeginWriting();
    if (FAILED(hr)) return fail("Couldn't start the MP3 encoder", hr);

    const size_t frames = samples.size() / 2;
    const size_t chunk = rate;  // one second at a time
    for (size_t start = 0; start < frames; start += chunk) {
        const size_t count = std::min(chunk, frames - start);
        ComPtr<IMFMediaBuffer> buffer;
        hr = MFCreateMemoryBuffer(DWORD(count * 4), &buffer);
        if (FAILED(hr)) return fail("Out of memory", hr);
        BYTE* data = nullptr;
        buffer->Lock(&data, nullptr, nullptr);
        auto* out = reinterpret_cast<int16_t*>(data);
        for (size_t i = 0; i < count * 2; ++i)
            out[i] = int16_t(std::lround(std::clamp(samples[start * 2 + i], -1.f, 1.f) * 32767.0f));
        buffer->Unlock();
        buffer->SetCurrentLength(DWORD(count * 4));

        ComPtr<IMFSample> sample;
        MFCreateSample(&sample);
        sample->AddBuffer(buffer.Get());
        sample->SetSampleTime(LONGLONG(start * 10000000ULL / rate));
        sample->SetSampleDuration(LONGLONG(count * 10000000ULL / rate));
        hr = writer->WriteSample(stream, sample.Get());
        if (FAILED(hr)) return fail("Encoding failed", hr);
    }
    hr = writer->Finalize();
    if (FAILED(hr)) return fail("Couldn't finish the MP3 file", hr);
    return true;
}

}  // namespace

std::vector<wxString> ExportFilePaths(const Project& project, const ExportOptions& options) {
    std::vector<wxString> paths;
    for (const Job& job : BuildJobs(project, options)) paths.push_back(job.path);
    return paths;
}

bool RunExport(wxWindow* parent, const Project& project, AudioEngine& engine, const ExportOptions& options,
               wxString* error) {
    const std::vector<Job> jobs = BuildJobs(project, options);
    constexpr int kUnits = 1000;
    const bool needsAudio = options.wav || options.mp3;
    // Rendering takes most of the time, so it gets as much of the bar as all the files together.
    const int renderUnits = needsAudio ? int(jobs.size()) * kUnits : 0;
    wxProgressDialog progress("Exporting", "Preparing...", renderUnits + int(jobs.size()) * kUnits, parent,
                              wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_SMOOTH | wxPD_ELAPSED_TIME);

    // An off-screen engraver lays out each part without disturbing the score on screen.
    auto* engraver = new ScoreCanvas(parent);
    engraver->Hide();
    struct Cleanup {
        ScoreCanvas* canvas;
        wxString temp;
        ~Cleanup() {
            canvas->Destroy();
            if (!temp.empty() && wxFileExists(temp)) wxRemoveFile(temp);
        }
    } cleanup{engraver, wxString()};

    // One pass captures the full mix and every part, so more parts don't mean more waiting.
    std::vector<float> mix;
    std::vector<std::vector<float>> stems;
    if (needsAudio) {
        const wxString message = engine.BounceIsRealTime() ? "Rendering audio at playback speed for your plugins..."
                                                           : "Rendering audio...";
        if (!progress.Update(0, message)) return false;
        if (!engine.Bounce(mix, stems, [&](double fraction) { return progress.Update(int(fraction * (renderUnits - 1)), message); }))
            return false;
    }

    for (size_t j = 0; j < jobs.size(); ++j) {
        const Job& job = jobs[j];
        const int baseUnits = renderUnits + int(j) * kUnits;
        const wxString name = wxFileName(job.path).GetFullName();
        // The extension stays last because the MP3 writer and readers care about it.
        const wxString temp = job.path.BeforeLast('.') + ".exporting." + job.path.AfterLast('.');
        cleanup.temp = temp;

        bool ok = true;
        wxString why;
        if (job.kind == Kind::Pdf) {
            engraver->SetProject(job.track < 0 ? project : MakePart(project, job.track));
            PdfWriter pdf(temp);
            ok = pdf.IsOk();
            const int pages = engraver->PageCount();
            for (int page = 0; ok && page < pages; ++page) {
                if (!progress.Update(baseUnits + page * kUnits / pages, "Engraving " + name)) return false;
                ok = pdf.AddPage(engraver->RenderPage(page, kPagePixels));
            }
            ok = ok && pdf.Finish();
        } else {
            if (!progress.Update(baseUnits, "Writing " + name)) return false;
            const std::vector<float>& audio = job.track < 0 ? mix : stems[job.track];
            const int rate = int(engine.SampleRate());
            ok = job.kind == Kind::Wav ? WriteWav(temp, audio, rate) : WriteMp3(temp, audio, rate, &why);
        }

        if (!ok) {
            if (error) *error = "Couldn't write " + name + (why.empty() ? wxString(".") : ": " + why);
            return false;
        }
        if (!wxRenameFile(temp, job.path, true)) {
            if (error) *error = "Couldn't save " + name + ". Is it open in another program?";
            return false;
        }
        cleanup.temp.clear();
    }
    progress.Update(renderUnits + int(jobs.size()) * kUnits);
    return true;
}

ExportDialog::ExportDialog(wxWindow* parent, const Project& project, const wxString& startFolder)
    : wxDialog(parent, wxID_ANY, "Export", wxDefaultPosition, wxSize(520, 560),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    SetBackgroundColour(kPanel);

    const auto heading = [this](const wxString& text) {
        auto* label = new wxStaticText(this, wxID_ANY, text);
        label->SetForegroundColour(kMuted);
        return label;
    };

    fullScore_ = new wxCheckBox(this, wxID_ANY, "Full score");
    fullScore_->SetValue(true);
    parts_ = new wxCheckBox(this, wxID_ANY, "Individual parts");
    parts_->SetValue(true);
    partList_ = new wxCheckListBox(this, wxID_ANY);
    for (const TrackDef& track : project.tracks) partList_->Append(track.name);
    for (unsigned i = 0; i < partList_->GetCount(); ++i) partList_->Check(i, true);

    pdf_ = new wxCheckBox(this, wxID_ANY, "PDF");
    wav_ = new wxCheckBox(this, wxID_ANY, "WAV");
    mp3_ = new wxCheckBox(this, wxID_ANY, "MP3");
    pdf_->SetValue(true);
    wav_->SetValue(true);
    mp3_->SetValue(true);

    folder_ = new wxDirPickerCtrl(this, wxID_ANY, startFolder, "Choose a folder", wxDefaultPosition, wxDefaultSize,
                                  wxDIRP_DEFAULT_STYLE | wxDIRP_USE_TEXTCTRL);

    auto* formats = new wxBoxSizer(wxHORIZONTAL);
    formats->Add(pdf_, 0, wxRIGHT, 18);
    formats->Add(wav_, 0, wxRIGHT, 18);
    formats->Add(mp3_);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(heading("What to export"), 0, wxLEFT | wxTOP | wxRIGHT, 16);
    sizer->Add(fullScore_, 0, wxLEFT | wxTOP | wxRIGHT, 10);
    sizer->Add(parts_, 0, wxLEFT | wxTOP | wxRIGHT, 10);
    sizer->Add(partList_, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);
    sizer->Add(heading("Formats"), 0, wxLEFT | wxTOP | wxRIGHT, 16);
    sizer->Add(formats, 0, wxLEFT | wxTOP | wxRIGHT, 10);
    sizer->Add(heading("Save to"), 0, wxLEFT | wxTOP | wxRIGHT, 16);
    sizer->Add(folder_, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);
    sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 16);
    SetSizer(sizer);

    export_ = static_cast<wxButton*>(FindWindow(wxID_OK));
    export_->SetLabel("Export");

    const auto refresh = [this](wxCommandEvent&) { UpdateState(); };
    for (wxCheckBox* box : {fullScore_, parts_, pdf_, wav_, mp3_}) box->Bind(wxEVT_CHECKBOX, refresh);
    partList_->Bind(wxEVT_CHECKLISTBOX, refresh);
    UpdateState();
    CentreOnParent();
}

void ExportDialog::UpdateState() {
    partList_->Enable(parts_->GetValue());
    wxArrayInt checked;
    const bool anyPart = parts_->GetValue() && partList_->GetCheckedItems(checked) > 0;
    export_->Enable((fullScore_->GetValue() || anyPart) && (pdf_->GetValue() || wav_->GetValue() || mp3_->GetValue()));
}

ExportOptions ExportDialog::GetOptions() const {
    ExportOptions options;
    options.folder = folder_->GetPath();
    options.fullScore = fullScore_->GetValue();
    if (parts_->GetValue()) {
        wxArrayInt checked;
        partList_->GetCheckedItems(checked);
        for (int item : checked) options.partTracks.push_back(item);
    }
    options.pdf = pdf_->GetValue();
    options.wav = wav_->GetValue();
    options.mp3 = mp3_->GetValue();
    return options;
}
