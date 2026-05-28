#ifndef MAINPRESENTER_HPP
#define MAINPRESENTER_HPP

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

    virtual void onPollResult(const MB_PollResult &result) override;

    virtual ~MainPresenter() {}

private:
    MainPresenter();

    MainView& view;
};

#endif // MAINPRESENTER_HPP
