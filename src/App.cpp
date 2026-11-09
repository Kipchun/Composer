#include "MainFrame.h"

class ComposerApp : public wxApp {
public:
    bool OnInit() override {
        MSWEnableDarkMode(DarkMode_Always);
        (new MainFrame(argc > 1 ? wxString(argv[1]) : wxString()))->Show();
        return true;
    }
};

wxIMPLEMENT_APP(ComposerApp);
