#include "director/DirectorSettings.h"

#include <wx/config.h>
#include <wx/secretstore.h>
#include <wx/sizer.h>

#include <nlohmann/json.hpp>

#include "Theme.h"
#include "director/DirectorClient.h"

using namespace ui;

namespace {

const char* const kService = "Composer/Director";
const char* const kModelSetting = "/Director/Model";

struct ModelInfo {
    const char* id;
    const char* name;
    double input, output, cacheRead;  // dollars per million tokens
};
const ModelInfo kModels[] = {
    {"claude-opus-5-5", "Claude Opus 5.5 (best results)", 4, 20, 0.2},
    {"claude-sonnet-5-5", "Claude Sonnet 5.5 (faster, cheaper)", 2, 10, 0.2},
    {"claude-haiku-5-5", "Claude Haiku 5.5 (fastest, cheapest)", 0.1, 0.5, 0.01},
};

const ModelInfo& FindModel(const wxString& id) {
    for (const ModelInfo& model : kModels)
        if (id == model.id) return model;
    return kModels[0];
}

class SettingsDialog : public wxDialog {
public:
    explicit SettingsDialog(wxWindow* parent) : wxDialog(parent, wxID_ANY, "Director Settings") {
        SetBackgroundColour(kPanel);
        const bool hasKey = !LoadDirectorKey().empty();

        auto label = [this](const wxString& text, const wxColour& color = kMuted) {
            auto* item = new wxStaticText(this, wxID_ANY, text);
            item->SetForegroundColour(color);
            return item;
        };

        key_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(420, -1), wxTE_PASSWORD);
        key_->SetHint(hasKey ? "A key is saved. Paste a new one to replace it." : "Paste your Claude API key");

        wxArrayString names;
        for (const ModelInfo& model : kModels) names.Add(model.name);
        model_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, names);
        const wxString current = LoadDirectorModel();
        for (int i = 0; i < int(WXSIZEOF(kModels)); ++i)
            if (current == kModels[i].id) model_->SetSelection(i);

        status_ = label("Your key is stored in Windows Credential Manager and is never saved in project files.");
        auto* test = new wxButton(this, wxID_ANY, "Test Connection");
        auto* remove = new wxButton(this, wxID_ANY, "Remove Key");

        auto* grid = new wxFlexGridSizer(2, 10, 12);
        grid->AddGrowableCol(1, 1);
        grid->Add(label("API key"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(key_, 1, wxEXPAND);
        grid->Add(label("Model"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(model_, 1, wxEXPAND);

        auto* tools = new wxBoxSizer(wxHORIZONTAL);
        tools->Add(test, 0, wxRIGHT, 8);
        tools->Add(remove);

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(grid, 0, wxEXPAND | wxALL, 16);
        sizer->Add(tools, 0, wxLEFT | wxRIGHT, 16);
        sizer->Add(status_, 0, wxEXPAND | wxALL, 16);
        sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
        SetSizerAndFit(sizer);
        CentreOnParent();

        static_cast<wxButton*>(FindWindow(wxID_OK))->SetLabel("Save");
        test->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Test(); });
        remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Remove(); });
        Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { Save(e); }, wxID_OK);

        client_.onText = [](const std::string&) {};
        client_.onComplete = [this](nlohmann::json, const std::string&, const DirectorUsage&) {
            status_->SetLabel("Connected successfully.");
        };
        client_.onError = [this](int status, const wxString& message) {
            status_->SetLabel(status == 401 ? wxString("That key was rejected.") : message);
        };
    }

    ~SettingsDialog() override { client_.Cancel(); }

private:
    void Test() {
        wxString key = key_->GetValue().Strip(wxString::both);
        if (key.empty()) key = LoadDirectorKey();
        if (key.empty()) {
            status_->SetLabel("Paste a key first.");
            return;
        }
        nlohmann::json body = {
            {"model", kModels[model_->GetSelection()].id},
            {"max_tokens", 16},
            {"stream", true},
            {"output_config", {{"effort", "low"}}},
            {"messages", nlohmann::json::array({{{"role", "user"}, {"content", "Say OK."}}})},
        };
        status_->SetLabel("Testing...");
        client_.Send(body, key);
    }

    void Remove() {
        wxSecretStore::GetDefault().Delete(kService);
        key_->Clear();
        key_->SetHint("Paste your Claude API key");
        status_->SetLabel("Key removed.");
    }

    void Save(wxCommandEvent& event) {
        const wxString key = key_->GetValue().Strip(wxString::both);
        if (!key.empty()) {
            wxSecretStore store = wxSecretStore::GetDefault();
            wxString error;
            if (!store.IsOk(&error) || !store.Save(kService, "api-key", wxSecretValue(key))) {
                status_->SetLabel("Couldn't save the key to the credential store.");
                return;
            }
        }
        wxConfigBase::Get()->Write(kModelSetting, wxString(kModels[model_->GetSelection()].id));
        wxConfigBase::Get()->Flush();
        event.Skip();
    }

    wxTextCtrl* key_;
    wxChoice* model_;
    wxStaticText* status_;
    DirectorClient client_;
};

}  // namespace

wxString LoadDirectorKey() {
    wxSecretStore store = wxSecretStore::GetDefault();
    if (!store.IsOk()) return wxString();
    wxString user;
    wxSecretValue secret;
    if (!store.Load(kService, user, secret) || !secret.IsOk()) return wxString();
    return secret.GetAsString(wxConvUTF8);
}

wxString LoadDirectorModel() {
    wxString id = wxConfigBase::Get()->Read(kModelSetting, wxString(kModels[0].id));
    return FindModel(id).id;
}

wxString DirectorModelName(const wxString& modelId) {
    return wxString(FindModel(modelId).name).BeforeFirst('(').Strip(wxString::both);
}

double DirectorCost(const wxString& modelId, long input, long output, long cacheRead, long cacheWrite) {
    const ModelInfo& m = FindModel(modelId);
    return (input * m.input + output * m.output + cacheRead * m.cacheRead + cacheWrite * m.input * 1.25) / 1e6;
}

bool ShowDirectorSettings(wxWindow* parent) {
    SettingsDialog dialog(parent);
    return dialog.ShowModal() == wxID_OK;
}
