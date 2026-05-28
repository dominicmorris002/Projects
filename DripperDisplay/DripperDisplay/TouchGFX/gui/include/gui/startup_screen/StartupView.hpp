#ifndef STARTUPVIEW_HPP
#define STARTUPVIEW_HPP

#include <gui_generated/startup_screen/StartupViewBase.hpp>
#include <gui/startup_screen/StartupPresenter.hpp>

class StartupView : public StartupViewBase
{
public:
    StartupView();
    virtual ~StartupView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();   // tick-based auto-advance to Main screen

protected:
    int tickCounter;
    static const int SPLASH_TICKS = 180;  // 3 seconds @ 60 fps
};

#endif // STARTUPVIEW_HPP