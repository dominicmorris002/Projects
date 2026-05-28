#ifndef PROTECTION_DISABLE_CONFIRMATION_SCREENVIEW_HPP
#define PROTECTION_DISABLE_CONFIRMATION_SCREENVIEW_HPP

#include <gui_generated/protection_disable_confirmation_screen_screen/Protection_Disable_Confirmation_ScreenViewBase.hpp>
#include <gui/protection_disable_confirmation_screen_screen/Protection_Disable_Confirmation_ScreenPresenter.hpp>

class Protection_Disable_Confirmation_ScreenView : public Protection_Disable_Confirmation_ScreenViewBase
{
public:
    Protection_Disable_Confirmation_ScreenView();
    virtual ~Protection_Disable_Confirmation_ScreenView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
};

#endif // PROTECTION_DISABLE_CONFIRMATION_SCREENVIEW_HPP
