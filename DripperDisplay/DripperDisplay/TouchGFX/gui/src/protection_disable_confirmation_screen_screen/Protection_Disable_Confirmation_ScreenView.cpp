#include <gui/protection_disable_confirmation_screen_screen/Protection_Disable_Confirmation_ScreenView.hpp>

#ifdef TARGET_STM32
#include "modbus_master.hpp"
#endif

Protection_Disable_Confirmation_ScreenView::Protection_Disable_Confirmation_ScreenView()
{
}

void Protection_Disable_Confirmation_ScreenView::setupScreen()
{
    Protection_Disable_Confirmation_ScreenViewBase::setupScreen();
}

void Protection_Disable_Confirmation_ScreenView::tearDownScreen()
{
    Protection_Disable_Confirmation_ScreenViewBase::tearDownScreen();
}

void Protection_Disable_Confirmation_ScreenView::ContinuePress()
{
#ifdef TARGET_STM32
    (void)mb_write_holding_u16(MB_HOLD_LOW_DRIP_SHDN_EN, 0u);
#endif
    application().gotoSettingsScreenNoTransition();
}
