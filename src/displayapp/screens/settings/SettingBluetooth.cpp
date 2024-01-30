#include "displayapp/screens/settings/SettingBluetooth.h"
#include <lvgl/lvgl.h>
#include "displayapp/DisplayApp.h"
#include "displayapp/Messages.h"
#include "displayapp/screens/Styles.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/screens/RadioList.h"

using namespace Pinetime::Applications::Screens;

namespace {
  struct Option {
    const char* name;
    bool radioEnabled;
  };

  constexpr std::array<Option, 2> options = {{
    {"Enabled", true},
    {"Disabled", false},
  }};

  std::array<RadioList::Item, RadioList::MaxItems> CreateOptionArray() {
    std::array<Pinetime::Applications::Screens::RadioList::Item, RadioList::MaxItems> optionArray;
    for (size_t i = 0; i < RadioList::MaxItems; i++) {
      if (i >= options.size()) {
        optionArray[i].name = "";
        optionArray[i].enabled = false;
      } else {
        optionArray[i].name = options[i].name;
        optionArray[i].enabled = true;
      }
    }
    return optionArray;
  };
}

SettingBluetooth::SettingBluetooth(Pinetime::Applications::DisplayApp* app, Pinetime::Controllers::Settings& settingsController)
  : app {app},
    settings {settingsController},
    radioList(
      0,
      1,
      "Bluetooth",
      Symbols::bluetooth,
      settingsController.GetBleRadioEnabled() ? 0 : 1,
      [this](uint32_t index) {
        const bool priorMode = settings.GetBleRadioEnabled();
        const bool newMode = options[index].radioEnabled;
        if (newMode != priorMode) {
          settings.SetBleRadioEnabled(newMode);
          this->app->PushMessage(Pinetime::Applications::Display::Messages::BleRadioEnableToggle);
        }
      },
      CreateOptionArray()) {
}

SettingBluetooth::~SettingBluetooth() {
  lv_obj_clean(lv_scr_act());
}
