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
#include "displayapp/screens/Alarm.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/settings/Settings.h"
#include "components/alarm/AlarmController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"

using namespace Pinetime::Applications::Screens;
using Pinetime::Controllers::AlarmController;

namespace {
  void ValueChangedHandler(void* userData) {
    auto* screen = static_cast<Alarm*>(userData);
    screen->OnValueChanged();
  }

  // Helper function to format recurring days for display
  void FormatRecurringDays(uint8_t recurDays, char* buffer, size_t bufferSize) {
    if (recurDays == 0x00) {
      snprintf(buffer, bufferSize, "Once");
      return;
    }
    if (recurDays == 0x7F) {
      snprintf(buffer, bufferSize, "Daily");
      return;
    }

    // Find consecutive day ranges
    const char* dayNames[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    uint8_t pos = 0;

    // Check for weekend wrap-around (Sa-Su)
    // If both Saturday (6) and Sunday (0) are set, check if they form a continuous range
    bool hasSaturday = (recurDays & (1 << 6)) != 0;
    bool hasSunday = (recurDays & (1 << 0)) != 0;

    if (hasSaturday && hasSunday) {
      // Check if this is a wrapping range (no other days, or starts at Sa and ends at Su)
      // Find if there are consecutive days from Saturday backwards
      uint8_t satStart = 6;
      while (satStart > 0 && (recurDays & (1 << (satStart - 1)))) {
        satStart--;
      }
      // Find if there are consecutive days from Sunday forwards
      uint8_t sunEnd = 0;
      while (sunEnd < 6 && (recurDays & (1 << (sunEnd + 1)))) {
        sunEnd++;
      }

      // Check if this forms a continuous weekend range
      bool continuous = true;
      for (uint8_t d = satStart; d <= 6; d++) {
        if (!(recurDays & (1 << d))) {
          continuous = false;
          break;
        }
      }
      if (continuous) {
        for (uint8_t d = 0; d <= sunEnd; d++) {
          if (!(recurDays & (1 << d))) {
            continuous = false;
            break;
          }
        }
      }

      if (continuous) {
        // Format as wrapped range: "Sa-Su", "Fr-Mo", etc.
        pos += snprintf(buffer + pos, bufferSize - pos, "%s-%s", dayNames[satStart], dayNames[sunEnd]);

        // Mark these days as processed
        for (uint8_t d = satStart; d <= 6; d++) {
          recurDays &= ~(1 << d);
        }
        for (uint8_t d = 0; d <= sunEnd; d++) {
          recurDays &= ~(1 << d);
        }
      }
    }

    // Process remaining days in normal order
    uint8_t i = 0;
    while (i < 7 && pos < bufferSize - 1) {
      if (!(recurDays & (1 << i))) {
        i++;
        continue;
      }

      // Found a selected day - check if it's part of a range
      uint8_t rangeStart = i;
      uint8_t rangeEnd = i;
      while (rangeEnd < 6 && (recurDays & (1 << (rangeEnd + 1)))) {
        rangeEnd++;
      }

      // Add space if not first entry
      if (pos > 0) {
        buffer[pos++] = ' ';
      }

      // Format the range
      if (rangeEnd >= rangeStart + 1) {
        // Consecutive range of 2+ days: "Mo-Fr"
        pos += snprintf(buffer + pos, bufferSize - pos, "%s-%s", dayNames[rangeStart], dayNames[rangeEnd]);
      } else {
        // Single day: "Mo"
        pos += snprintf(buffer + pos, bufferSize - pos, "%s", dayNames[rangeStart]);
      }

      i = rangeEnd + 1;
    }
    buffer[pos] = '\0';
  }
}

static void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
  auto* screen = static_cast<Alarm*>(obj->user_data);
  screen->OnButtonEvent(obj, event);
}

static void StopAlarmTaskCallback(lv_task_t* task) {
  auto* screen = static_cast<Alarm*>(task->user_data);
  screen->StopAlerting();
}

static void launcherBtnEventHandler(lv_obj_t* obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    auto* screen = static_cast<Alarm*>(obj->user_data);
    screen->OnLauncherButtonClicked(obj);
  }
}

static void switchEventHandler(lv_obj_t* obj, lv_event_t event) {
  if (event == LV_EVENT_VALUE_CHANGED) {
    auto* screen = static_cast<Alarm*>(obj->user_data);
    screen->OnButtonEvent(obj, event);
  }
}

Alarm::Alarm(Controllers::AlarmController& alarmController,
             Controllers::Settings::ClockType clockType,
             System::SystemTask& systemTask,
             Controllers::MotorController& motorController)
  : alarmController {alarmController}, wakeLock(systemTask), motorController {motorController}, clockType {clockType} {

  // Decide which UI to show
  if (alarmController.IsAlerting()) {
    // If alarming, go directly to config mode for the alerting alarm
    launcherMode = false;
    selectedAlarmIndex = alarmController.AlertingAlarmIndex();
    CreateAlarmConfigUI(selectedAlarmIndex);
    SetAlerting();
  } else {
    // Show launcher by default
    launcherMode = true;
    CreateLauncherUI();
  }
}

void Alarm::CreateLauncherUI() {
  static constexpr lv_color_t bgColor = Colors::bgAlt;
  static constexpr uint8_t btnHeight = 55;
  static constexpr uint8_t btnSpacing = 5;

  lv_style_init(&launcherButtonStyle);
  lv_style_set_radius(&launcherButtonStyle, LV_STATE_DEFAULT, 10);
  lv_style_set_bg_color(&launcherButtonStyle, LV_STATE_DEFAULT, bgColor);

  for (uint8_t i = 0; i < Controllers::AlarmController::MaxAlarms; i++) {
    // Create button container
    alarmButtons[i] = lv_btn_create(lv_scr_act(), nullptr);
    alarmButtons[i]->user_data = this;
    lv_obj_set_event_cb(alarmButtons[i], launcherBtnEventHandler);
    lv_obj_set_size(alarmButtons[i], 150, btnHeight);
    lv_obj_add_style(alarmButtons[i], LV_BTN_PART_MAIN, &launcherButtonStyle);
    lv_obj_align(alarmButtons[i], lv_scr_act(), LV_ALIGN_IN_LEFT_MID, 5, -90 + (i * (btnHeight + btnSpacing)));

    // Create time label
    alarmTimeLabels[i] = lv_label_create(alarmButtons[i], nullptr);
    lv_obj_set_style_local_text_font(alarmTimeLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    uint8_t hours = alarmController.Hours(i);
    if (clockType == Controllers::Settings::ClockType::H12) {
      uint8_t displayHours = hours % 12;
      if (displayHours == 0) {
        displayHours = 12;
      }
      lv_label_set_text_fmt(alarmTimeLabels[i], "%2hhu:%02hhu %s", displayHours, alarmController.Minutes(i), hours < 12 ? "AM" : "PM");
    } else {
      lv_label_set_text_fmt(alarmTimeLabels[i], "%02hhu:%02hhu", hours, alarmController.Minutes(i));
    }
    lv_obj_align(alarmTimeLabels[i], alarmButtons[i], LV_ALIGN_CENTER, 0, -10);

    // Create recurrence label
    alarmRecurLabels[i] = lv_label_create(alarmButtons[i], nullptr);
    lv_obj_set_style_local_text_font(alarmRecurLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_obj_set_style_local_text_opa(alarmRecurLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_60);

    // Format recurring days with ranges
    static char dayText[32];
    FormatRecurringDays(alarmController.GetRecurDays(i), dayText, sizeof(dayText));
    lv_label_set_text(alarmRecurLabels[i], dayText);
    lv_obj_align(alarmRecurLabels[i], alarmButtons[i], LV_ALIGN_CENTER, 0, 12);

    // Create enable/disable switch
    alarmSwitches[i] = lv_switch_create(lv_scr_act(), nullptr);
    alarmSwitches[i]->user_data = this;
    lv_obj_set_event_cb(alarmSwitches[i], switchEventHandler);
    lv_obj_set_size(alarmSwitches[i], 70, 40);
    lv_obj_set_style_local_bg_color(alarmSwitches[i], LV_SWITCH_PART_BG, LV_STATE_DEFAULT, bgColor);
    lv_obj_align(alarmSwitches[i], lv_scr_act(), LV_ALIGN_IN_RIGHT_MID, -5, -90 + (i * (btnHeight + btnSpacing)));

    if (alarmController.IsEnabled(i)) {
      lv_switch_on(alarmSwitches[i], LV_ANIM_OFF);
    } else {
      lv_switch_off(alarmSwitches[i], LV_ANIM_OFF);
    }
  }
}

void Alarm::CreateAlarmConfigUI(uint8_t alarmIndex) {
  selectedAlarmIndex = alarmIndex;

  hourCounter.Create();
  lv_obj_align(hourCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_LEFT, 34, 0);
  if (clockType == Controllers::Settings::ClockType::H12) {
    hourCounter.EnableTwelveHourMode();

    lblampm = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_font(lblampm, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_static(lblampm, "AM");
    lv_label_set_align(lblampm, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(lblampm, lv_scr_act(), LV_ALIGN_CENTER, 0, 30);
  }
  hourCounter.SetValue(alarmController.Hours(alarmIndex));
  hourCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  minuteCounter.Create();
  lv_obj_align(minuteCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_RIGHT, -34, 0);
  minuteCounter.SetValue(alarmController.Minutes(alarmIndex));
  minuteCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  lv_obj_t* colonLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(colonLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_label_set_text_static(colonLabel, ":");
  lv_obj_align(colonLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -52);

  progressStop = lv_bar_create(lv_scr_act(), nullptr);
  lv_bar_set_range(progressStop, 0, progressBarSize);
  lv_bar_set_value(progressStop, 0, LV_ANIM_OFF);
  lv_obj_set_size(progressStop, progressBarSize, 10);
  lv_obj_align(progressStop, nullptr, LV_ALIGN_IN_TOP_MID, 0, 0);
  lv_obj_set_style_local_bg_color(progressStop, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_obj_set_hidden(progressStop, true);

  btnStop = lv_btn_create(lv_scr_act(), nullptr);
  btnStop->user_data = this;
  lv_obj_set_event_cb(btnStop, btnEventHandler);
  lv_obj_set_size(btnStop, 240, 70);
  lv_obj_align(btnStop, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_local_bg_color(btnStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  txtStop = lv_label_create(btnStop, nullptr);
  lv_label_set_text_static(txtStop, Symbols::stop);
  lv_obj_set_hidden(btnStop, true);

  static constexpr lv_color_t bgColor = Colors::bgAlt;

  btnInfo = lv_btn_create(lv_scr_act(), nullptr);
  btnInfo->user_data = this;
  lv_obj_set_event_cb(btnInfo, btnEventHandler);
  lv_obj_set_size(btnInfo, 50, 50);
  lv_obj_align(btnInfo, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, -4);
  lv_obj_set_style_local_bg_color(btnInfo, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, bgColor);
  lv_obj_set_style_local_border_width(btnInfo, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 4);
  lv_obj_set_style_local_border_color(btnInfo, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

  lv_obj_t* txtInfo = lv_label_create(btnInfo, nullptr);
  lv_label_set_text_static(txtInfo, "i");

  // Create 7 day-of-week checkboxes with labels
  const char* dayLabelText[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  for (uint8_t i = 0; i < 7; i++) {
    // Create label above checkbox
    dayLabels[i] = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text_static(dayLabels[i], dayLabelText[i]);
    lv_obj_set_style_local_text_font(dayLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_obj_align(dayLabels[i], lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 5 + (i * 34), -75);

    // Create checkbox below label
    dayCheckboxes[i] = lv_checkbox_create(lv_scr_act(), nullptr);
    lv_checkbox_set_text_static(dayCheckboxes[i], "");  // No text in checkbox itself
    dayCheckboxes[i]->user_data = this;
    lv_obj_set_event_cb(dayCheckboxes[i], btnEventHandler);
    lv_obj_set_size(dayCheckboxes[i], 30, 30);
    lv_obj_align(dayCheckboxes[i], lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 5 + (i * 34), -45);

    // Set checked state from bitmask
    if (alarmController.IsRecurDaySet(selectedAlarmIndex, i)) {
      lv_checkbox_set_checked(dayCheckboxes[i], true);
    }
  }

  btnBack = lv_btn_create(lv_scr_act(), nullptr);
  btnBack->user_data = this;
  lv_obj_set_event_cb(btnBack, btnEventHandler);
  lv_obj_set_size(btnBack, 115, 42);
  lv_obj_align(btnBack, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, -7, 0);
  lv_obj_set_style_local_bg_color(btnBack, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, bgColor);
  lv_obj_t* txtBack = lv_label_create(btnBack, nullptr);
  lv_label_set_text_static(txtBack, "Back");

  enableSwitch = lv_switch_create(lv_scr_act(), nullptr);
  enableSwitch->user_data = this;
  lv_obj_set_event_cb(enableSwitch, btnEventHandler);
  lv_obj_set_size(enableSwitch, 100, 42);
  lv_obj_align(enableSwitch, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 7, 0);
  lv_obj_set_style_local_bg_color(enableSwitch, LV_SWITCH_PART_BG, LV_STATE_DEFAULT, bgColor);

  UpdateAlarmTime();

  if (alarmController.IsAlerting()) {
    SetAlerting();
  } else {
    SetSwitchState(LV_ANIM_OFF);
  }

  taskRefresh = lv_task_create(RefreshTaskCallback, 50, LV_TASK_PRIO_MID, this);
}

void Alarm::Refresh() {
  if (stopBtnPressTime.has_value()) {
    TickType_t elapsed = xTaskGetTickCount() - stopBtnPressTime.value();
    if (elapsed >= longPressTimeout) {
      ResetStopProgress();
      StopAlerting();
      ReturnToLauncher();
    } else {
      lv_coord_t stopPosition = (elapsed * progressBarSize) / longPressTimeout;
      UpdateStopProgress(stopPosition);
    }
  }
}

void Alarm::OnLauncherButtonClicked(lv_obj_t* obj) {
  // Find which alarm button was clicked
  for (uint8_t i = 0; i < Controllers::AlarmController::MaxAlarms; i++) {
    if (obj == alarmButtons[i]) {
      // Switch to config mode for this alarm
      lv_style_reset(&launcherButtonStyle);
      lv_obj_clean(lv_scr_act());
      launcherMode = false;
      CreateAlarmConfigUI(i);
      return;
    }
  }
}

void Alarm::ReturnToLauncher() {
  lv_obj_clean(lv_scr_act());
  launcherMode = true;
  CreateLauncherUI();
}

Alarm::~Alarm() {
  if (alarmController.IsAlerting()) {
    StopAlerting();
  }
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
  alarmController.SaveAlarm();
}

void Alarm::DisableAlarm() {
  if (alarmController.IsEnabled(selectedAlarmIndex)) {
    alarmController.DisableAlarm(selectedAlarmIndex);
  }
}

void Alarm::StopButtonPressed() {
  stopBtnPressTime = xTaskGetTickCount();
  UpdateStopProgress(0);
  lv_obj_set_hidden(progressStop, false);
}

void Alarm::ResetStopProgress() {
  stopBtnPressTime = std::nullopt;
  lv_obj_set_hidden(progressStop, true);
  UpdateStopProgress(0);
}

void Alarm::UpdateStopProgress(lv_coord_t stopPosition) {
  lv_bar_set_value(progressStop, progressBarSize - stopPosition, LV_ANIM_OFF);
}

void Alarm::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  // Handle launcher mode switch events
  if (launcherMode) {
    if (event == LV_EVENT_VALUE_CHANGED) {
      for (uint8_t i = 0; i < Controllers::AlarmController::MaxAlarms; i++) {
        if (obj == alarmSwitches[i]) {
          alarmController.SetEnabled(i, lv_switch_get_state(alarmSwitches[i]));
          return;
        }
      }
    }
    return;
  }

  // Handle config mode events
  if (obj == btnStop) {
    if (event == LV_EVENT_PRESSED) {
      StopButtonPressed();
    } else if (event == LV_EVENT_RELEASED || event == LV_EVENT_PRESS_LOST) {
      ResetStopProgress();
    }
    return;
  }
  if (event == LV_EVENT_CLICKED) {
    if (obj == btnInfo) {
      ShowInfo();
      return;
    }
    if (obj == btnMessage) {
      HideInfo();
      return;
    }
    if (obj == btnBack) {
      ReturnToLauncher();
      return;
    }
  }
  if (event == LV_EVENT_VALUE_CHANGED) {
    if (obj == enableSwitch) {
      alarmController.SetEnabled(selectedAlarmIndex, lv_switch_get_state(enableSwitch));
      return;
    }
    // Handle checkbox events
    for (uint8_t i = 0; i < 7; i++) {
      if (obj == dayCheckboxes[i]) {
        OnDayCheckboxChanged(i);
        return;
      }
    }
  }
}

bool Alarm::OnButtonPushed() {
  if (txtMessage != nullptr && btnMessage != nullptr) {
    HideInfo();
    return true;
  }
  if (alarmController.IsAlerting()) {
    StopAlerting();
    ReturnToLauncher();
    return true;
  }
  // Hardware button returns to launcher in config mode
  if (!launcherMode) {
    ReturnToLauncher();
    return true;
  }
  return false;
}

bool Alarm::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  // Don't allow closing the screen by swiping while the alarm is alerting
  return alarmController.IsAlerting() && event == TouchEvents::SwipeDown;
}

void Alarm::OnValueChanged() {
  DisableAlarm();
  UpdateAlarmTime();
}

void Alarm::UpdateAlarmTime() {
  if (lblampm != nullptr) {
    if (hourCounter.GetValue() >= 12) {
      lv_label_set_text_static(lblampm, "PM");
    } else {
      lv_label_set_text_static(lblampm, "AM");
    }
  }
  alarmController.SetAlarmTime(selectedAlarmIndex, hourCounter.GetValue(), minuteCounter.GetValue());
}

void Alarm::SetAlerting() {
  lv_obj_set_hidden(enableSwitch, true);
  lv_obj_set_hidden(btnBack, true);
  lv_obj_set_hidden(btnInfo, true);
  for (uint8_t i = 0; i < 7; i++) {
    lv_obj_set_hidden(dayCheckboxes[i], true);
    lv_obj_set_hidden(dayLabels[i], true);
  }
  hourCounter.HideControls();
  minuteCounter.HideControls();
  lv_obj_set_hidden(btnStop, false);
  taskStopAlarm = lv_task_create(StopAlarmTaskCallback, pdMS_TO_TICKS(60 * 1000), LV_TASK_PRIO_MID, this);
  motorController.StartRinging();
  wakeLock.Lock();
}

void Alarm::StopAlerting() {
  alarmController.StopAlerting();
  motorController.StopRinging();
  if (taskStopAlarm != nullptr) {
    lv_task_del(taskStopAlarm);
    taskStopAlarm = nullptr;
  }
  wakeLock.Release();
  lv_indev_wait_release(lv_indev_get_act());
}

void Alarm::SetSwitchState(lv_anim_enable_t anim) {
  if (alarmController.IsEnabled(selectedAlarmIndex)) {
    lv_switch_on(enableSwitch, anim);
  } else {
    lv_switch_off(enableSwitch, anim);
  }
}

void Alarm::ShowInfo() {
  if (btnMessage != nullptr) {
    return;
  }
  btnMessage = lv_btn_create(lv_scr_act(), nullptr);
  btnMessage->user_data = this;
  lv_obj_set_event_cb(btnMessage, btnEventHandler);
  lv_obj_set_height(btnMessage, 200);
  lv_obj_set_width(btnMessage, 150);
  lv_obj_align(btnMessage, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  txtMessage = lv_label_create(btnMessage, nullptr);
  lv_obj_set_style_local_bg_color(btnMessage, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_NAVY);

  if (alarmController.IsEnabled(selectedAlarmIndex)) {
    auto timeToAlarm = alarmController.SecondsToAlarm();

    auto daysToAlarm = timeToAlarm / 86400;
    auto hrsToAlarm = (timeToAlarm % 86400) / 3600;
    auto minToAlarm = (timeToAlarm % 3600) / 60;
    auto secToAlarm = timeToAlarm % 60;

    lv_label_set_text_fmt(txtMessage,
                          "Time to\nalarm:\n%2lu Days\n%2lu Hours\n%2lu Minutes\n%2lu Seconds",
                          daysToAlarm,
                          hrsToAlarm,
                          minToAlarm,
                          secToAlarm);
  } else {
    lv_label_set_text_static(txtMessage, "Alarm\nis not\nset.");
  }
}

void Alarm::HideInfo() {
  lv_obj_del(btnMessage);
  txtMessage = nullptr;
  btnMessage = nullptr;
}

void Alarm::OnDayCheckboxChanged(uint8_t dayOfWeek) {
  uint8_t currentDays = alarmController.GetRecurDays(selectedAlarmIndex);
  if (lv_checkbox_is_checked(dayCheckboxes[dayOfWeek])) {
    // Set the bit for this day
    currentDays |= (1 << dayOfWeek);
  } else {
    // Clear the bit for this day
    currentDays &= ~(1 << dayOfWeek);
  }
  alarmController.SetRecurDays(selectedAlarmIndex, currentDays);

  // Reschedule to update next alarm time if this alarm is enabled
  if (alarmController.IsEnabled(selectedAlarmIndex)) {
    alarmController.ScheduleAlarm();
  }
}
