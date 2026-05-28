#ifndef PROTECTION_DISABLE_CONFIRMATION_SCREENPRESENTER_HPP
#define PROTECTION_DISABLE_CONFIRMATION_SCREENPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Protection_Disable_Confirmation_ScreenView;

class Protection_Disable_Confirmation_ScreenPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    Protection_Disable_Confirmation_ScreenPresenter(Protection_Disable_Confirmation_ScreenView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~Protection_Disable_Confirmation_ScreenPresenter() {}

private:
    Protection_Disable_Confirmation_ScreenPresenter();

    Protection_Disable_Confirmation_ScreenView& view;
};

#endif // PROTECTION_DISABLE_CONFIRMATION_SCREENPRESENTER_HPP
