#include "displayapp/screens/RadioList.h"
#include "displayapp/screens/Styles.h"

using namespace Pinetime::Applications::Screens;

namespace {
  static void event_handler(lv_obj_t* obj, lv_event_t event) {
    RadioList* screen = static_cast<RadioList*>(obj->user_data);
    screen->UpdateSelected(obj, event);
  }
}

RadioList::RadioList(const uint8_t screenID,
                     const uint8_t numScreens,
                     const char* optionsTitle,
                     const char* optionsSymbol,
                     uint32_t originalValue,
                     std::function<void(uint32_t)> OnValueChanged,
                     std::array<Item, MaxItems> options) {
  RenderScreen(numScreens, optionsTitle, optionsSymbol);
  RenderOptions(screenID, originalValue, options);
}

void RadioList::RenderOptions(const uint8_t screenID,
                              uint32_t originalValue,
                              std::array<Item, MaxItems> options) {
  for (unsigned int i = 0; i < options.size(); i++) {
    if (strcmp(options[i].name, "")) {
      cbOption[i] = lv_checkbox_create(container1, nullptr);
      lv_checkbox_set_text(cbOption[i], options[i].name);
      if (!options[i].enabled) {
        lv_checkbox_set_disabled(cbOption[i]);
      }
      cbOption[i]->user_data = this;
      lv_obj_set_event_cb(cbOption[i], event_handler);
      SetRadioButtonStyle(cbOption[i]);

      if (static_cast<unsigned int>(originalValue - MaxItems * screenID) == i) {
        lv_checkbox_set_checked(cbOption[i], true);
      }
    }
  }
}

void RadioList::UpdateSelected(lv_obj_t* object, lv_event_t event) {
  if (event == LV_EVENT_VALUE_CHANGED) {
    for (unsigned int i = 0; i < options.size(); i++) {
      if (strcmp(options[i].name, "")) {
        if (object == cbOption[i]) {
          lv_checkbox_set_checked(cbOption[i], true);
          value = MaxItems * screenID + i;
        } else {
          lv_checkbox_set_checked(cbOption[i], false);
        }
        if (!options[i].enabled) {
          lv_checkbox_set_disabled(cbOption[i]);
        }
      }
    }
  }
}
