#pragma once

#include <lvgl/lvgl.h>
#include "displayapp/screens/CheckboxList.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class RadioList : public CheckboxList {
        public:
          // using CheckboxList::CheckboxList;
          RadioList(const uint8_t screenID,
                    const uint8_t numScreens,
                    const char* optionsTitle,
                    const char* optionsSymbol,
                    uint32_t originalValue,
                    std::function<void(uint32_t)> OnValueChanged,
                    std::array<Item, MaxItems> options);

          void RenderOptions(const uint8_t screenID,
                             uint32_t originalValue,
                             std::array<Item, MaxItems> options) override;

          void UpdateSelected(lv_obj_t* object, lv_event_t event);
      };
    }
  }
}
