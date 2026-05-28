#ifndef MAINPRESENTER_HPP
#define MAINPRESENTER_HPP

#include <cstdint>                          // FIX 1: uint32_t
#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MainView;

class MainPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MainPresenter(MainView& v);

    virtual void activate();
    virtual void deactivate();
    virtual void handleTickEvent();         // FIX 2: declare it

    virtual ~MainPresenter() {}

private:
    MainPresenter();
    // FIX 3: removed duplicate 'private:' keyword
    uint32_t tickCounter;
    MainView& view;
};

#endif // MAINPRESENTER_HPP