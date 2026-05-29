#include <gui/common/FrontendApplication.hpp>
#include <gui/common/FrontendHeap.hpp>
#include <gui/main_screen/MainView.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/protection_disable_confirmation_screen_screen/Protection_Disable_Confirmation_ScreenView.hpp>
#include <gui/protection_disable_confirmation_screen_screen/Protection_Disable_Confirmation_ScreenPresenter.hpp>
#include <touchgfx/transitions/NoTransition.hpp>

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap),
      mainNoTransCallback(),
      protectionTransitionCallback()
{
}

void FrontendApplication::gotoMainScreenNoTransition()
{
    mainNoTransCallback =
        touchgfx::Callback<FrontendApplication>(
            this, &FrontendApplication::gotoMainScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &mainNoTransCallback;
}

void FrontendApplication::gotoMainScreenNoTransitionImpl()
{
    touchgfx::makeTransition<MainView, MainPresenter, touchgfx::NoTransition, Model>(
        &currentScreen, &currentPresenter, static_cast<touchgfx::MVPHeap &>(frontendHeap),
        &currentTransition, &model);
}

void FrontendApplication::gotoProtection_Disable_Confirmation_ScreenNoTransition()
{
    protectionTransitionCallback =
        touchgfx::Callback<FrontendApplication>(
            this,
            &FrontendApplication::gotoProtection_Disable_Confirmation_ScreenNoTransitionImpl);
    pendingScreenTransitionCallback = &protectionTransitionCallback;
}

void FrontendApplication::gotoProtection_Disable_Confirmation_ScreenNoTransitionImpl()
{
    touchgfx::makeTransition<
        Protection_Disable_Confirmation_ScreenView,
        Protection_Disable_Confirmation_ScreenPresenter,
        touchgfx::NoTransition,
        Model>(&currentScreen, &currentPresenter, static_cast<touchgfx::MVPHeap&>(frontendHeap),
               &currentTransition, &model);
}
