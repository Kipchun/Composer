#include "MainFrame.h"

class ComposerApp : public wxApp {
public:
    bool OnInit() override {
        MSWEnableDarkMode(DarkMode_Always);
        (new MainFrame())->Show();
        return true;
    }
};

wxIMPLEMENT_APP(ComposerApp);
