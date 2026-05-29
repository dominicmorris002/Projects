#include <gui/settings_screen/SettingsPresenter.hpp>
#include <gui/settings_screen/SettingsView.hpp>

SettingsPresenter::SettingsPresenter(SettingsView &v)
    : view(v)
{
}

void SettingsPresenter::activate()
{
    view.refreshFromPlc();
}

void SettingsPresenter::deactivate()
{
}
