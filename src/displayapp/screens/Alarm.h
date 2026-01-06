/*  Copyright (C) 2021 mruss77, Florian

    This file is part of InfiniTime.

    InfiniTime is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    InfiniTime is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once

#include "displayapp/apps/Apps.h"
#include "components/settings/Settings.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/widgets/Counter.h"
#include "displayapp/Controllers.h"
#include "systemtask/WakeLock.h"
#include "Symbols.h"
#include <optional>

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class Alarm : public Screen {
      public:
        explicit Alarm(Controllers::AlarmController& alarmController,
                       Controllers::Settings::ClockType clockType,
                       System::SystemTask& systemTask,
                       Controllers::MotorController& motorController,
                       Controllers::Settings& settingsController);
        ~Alarm() override;
        void Refresh() override;
        void SetAlerting();
        void OnButtonEvent(lv_obj_t* obj, lv_event_t event);
        void OnLauncherButtonClicked(lv_obj_t* obj);
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;
        void OnValueChanged();
        void StopAlerting();
        void StopButtonPressed();
        void ResetStopProgress();

      private:
        Controllers::AlarmController& alarmController;
        System::WakeLock wakeLock;
        Controllers::MotorController& motorController;
        Controllers::Settings& settingsController;
        Controllers::Settings::ClockType clockType;

        bool launcherMode = true;
        uint8_t selectedAlarmIndex = 0;

        // Launcher mode elements
        lv_obj_t* alarmButtons[Controllers::AlarmController::MaxAlarms] = {nullptr};
        lv_obj_t* alarmTimeLabels[Controllers::AlarmController::MaxAlarms] = {nullptr};
        lv_obj_t* alarmRecurLabels[Controllers::AlarmController::MaxAlarms] = {nullptr};
        lv_obj_t* alarmSwitches[Controllers::AlarmController::MaxAlarms] = {nullptr};
        lv_style_t launcherButtonStyle;

        // Config mode elements
        lv_obj_t *btnStop, *txtStop, *btnInfo, *enableSwitch, *btnBack;
        lv_obj_t* dayCheckboxes[7] = {nullptr};
        lv_obj_t* dayLabels[7] = {nullptr};
        lv_obj_t* lblampm = nullptr;
        lv_obj_t* txtMessage = nullptr;
        lv_obj_t* btnMessage = nullptr;
        lv_task_t* taskRefresh = nullptr;
        lv_task_t* taskStopAlarm = nullptr;

        enum class EnableButtonState { On, Off, Alerting };
        void CreateLauncherUI();
        void CreateAlarmConfigUI(uint8_t alarmIndex);
        void DisableAlarm();
        void SetSwitchState(lv_anim_enable_t anim);
        void SetAlarm();
        void ShowInfo();
        void HideInfo();
        void UpdateAlarmTime();
        void ReturnToLauncher();
        void OnDayCheckboxChanged(uint8_t dayOfWeek);
        void UpdateStopProgress(lv_coord_t stopPosition);
        Widgets::Counter hourCounter = Widgets::Counter(0, 23, jetbrains_mono_42);
        Widgets::Counter minuteCounter = Widgets::Counter(0, 59, jetbrains_mono_42);

        lv_obj_t* progressStop;
        std::optional<TickType_t> stopBtnPressTime;
        static constexpr TickType_t longPressTimeout = pdMS_TO_TICKS(1000);
        static constexpr lv_coord_t progressBarSize = 240;
      };
    }

    template <>
    struct AppTraits<Apps::Alarm> {
      static constexpr Apps app = Apps::Alarm;
      static constexpr const char* icon = Screens::Symbols::bell;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Alarm(controllers.alarmController,
                                  controllers.settingsController.GetClockType(),
                                  *controllers.systemTask,
                                  controllers.motorController,
                                  controllers.settingsController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
