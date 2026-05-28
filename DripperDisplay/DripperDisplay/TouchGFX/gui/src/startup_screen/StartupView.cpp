#include <gui/startup_screen/StartupView.hpp>

StartupView::StartupView() : tickCounter(0)
{
}

void StartupView::setupScreen()
{
    StartupViewBase::setupScreen();
    tickCounter = 0;
}

void StartupView::tearDownScreen()
{
    StartupViewBase::tearDownScreen();
}

void StartupView::handleTickEvent()
{
    tickCounter++;
    if (tickCounter >= SPLASH_TICKS)
    {
        application().gotoMainScreenWipeTransitionEast();
    }
}
