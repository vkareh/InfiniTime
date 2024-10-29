#include "displayapp/screens/Sleep.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/InfiniTimeTheme.h"
#include "components/settings/Settings.h"
#include "components/alarm/AlarmController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"

using namespace Pinetime::Applications::Screens;
using Pinetime::Controllers::InfiniSleepController;

namespace {
  void ValueChangedHandler(void* userData) {
    auto* screen = static_cast<Sleep*>(userData);
    screen->OnValueChanged();
  }
}

static void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
  auto* screen = static_cast<Sleep*>(obj->user_data);
  screen->OnButtonEvent(obj, event);
}

static void StopAlarmTaskCallback(lv_task_t* task) {
  auto* screen = static_cast<Sleep*>(task->user_data);
  screen->StopAlerting();
}

Sleep::Sleep(Controllers::InfiniSleepController& infiniSleepController,
             Controllers::Settings::ClockType clockType,
             System::SystemTask& systemTask,
             Controllers::MotorController& motorController)
  : infiniSleepController {infiniSleepController}, wakeLock(systemTask), motorController {motorController} {

  hourCounter.Create();
  lv_obj_align(hourCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  if (clockType == Controllers::Settings::ClockType::H12) {
    hourCounter.EnableTwelveHourMode();

    lblampm = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_font(lblampm, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_static(lblampm, "AM");
    lv_label_set_align(lblampm, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(lblampm, lv_scr_act(), LV_ALIGN_CENTER, 0, 30);
  }
  hourCounter.SetValue(infiniSleepController.Hours());
  hourCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  minuteCounter.Create();
  lv_obj_align(minuteCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 0);
  minuteCounter.SetValue(infiniSleepController.Minutes());
  minuteCounter.SetValueChangedEventCallback(this, ValueChangedHandler);

  lv_obj_t* colonLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(colonLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
  lv_label_set_text_static(colonLabel, ":");
  lv_obj_align(colonLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -29);

  btnStop = lv_btn_create(lv_scr_act(), nullptr);
  btnStop->user_data = this;
  lv_obj_set_event_cb(btnStop, btnEventHandler);
  lv_obj_set_size(btnStop, 115, 50);
  lv_obj_align(btnStop, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_local_bg_color(btnStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  txtStop = lv_label_create(btnStop, nullptr);
  lv_label_set_text_static(txtStop, Symbols::stop);
  lv_obj_set_hidden(btnStop, true);

  static constexpr lv_color_t bgColor = Colors::bgAlt;

  btnRecur = lv_btn_create(lv_scr_act(), nullptr);
  btnRecur->user_data = this;
  lv_obj_set_event_cb(btnRecur, btnEventHandler);
  lv_obj_set_size(btnRecur, 115, 50);
  lv_obj_align(btnRecur, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, 0, 0);
  txtRecur = lv_label_create(btnRecur, nullptr);
  SetRecurButtonState();
  lv_obj_set_style_local_bg_color(btnRecur, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, bgColor);

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

  enableSwitch = lv_switch_create(lv_scr_act(), nullptr);
  enableSwitch->user_data = this;
  lv_obj_set_event_cb(enableSwitch, btnEventHandler);
  lv_obj_set_size(enableSwitch, 100, 50);
  // Align to the center of 115px from edge
  lv_obj_align(enableSwitch, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 7, 0);
  lv_obj_set_style_local_bg_color(enableSwitch, LV_SWITCH_PART_BG, LV_STATE_DEFAULT, bgColor);

  UpdateWakeAlarmTime();

  if (infiniSleepController.IsAlerting()) {
    SetAlerting();
  } else {
    SetSwitchState(LV_ANIM_OFF);
  }
}

Sleep::~Sleep() {
  if (infiniSleepController.IsAlerting()) {
    StopAlerting();
  }
  lv_obj_clean(lv_scr_act());
  infiniSleepController.SaveWakeAlarm();
}

void Sleep::DisableWakeAlarm() {
  if (infiniSleepController.IsEnabled()) {
    infiniSleepController.DisableWakeAlarm();
    lv_switch_off(enableSwitch, LV_ANIM_ON);
  }
}

void Sleep::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    if (obj == btnStop) {
      StopAlerting();
      return;
    }
    if (obj == btnInfo) {
      ShowAlarmInfo();
      return;
    }
    if (obj == btnMessage) {
      HideAlarmInfo();
      return;
    }
    if (obj == enableSwitch) {
      if (lv_switch_get_state(enableSwitch)) {
        infiniSleepController.ScheduleWakeAlarm();
      } else {
        infiniSleepController.DisableWakeAlarm();
      }
      return;
    }
    if (obj == btnRecur) {
      DisableWakeAlarm();
      ToggleRecurrence();
    }
  }
}

bool Sleep::OnButtonPushed() {
  if (txtMessage != nullptr && btnMessage != nullptr) {
    HideAlarmInfo();
    return true;
  }
  if (infiniSleepController.IsAlerting()) {
    StopAlerting();
    return true;
  }
  return false;
}

bool Sleep::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  // Don't allow closing the screen by swiping while the alarm is alerting
  return infiniSleepController.IsAlerting() && event == TouchEvents::SwipeDown;
}

void Sleep::OnValueChanged() {
  DisableWakeAlarm();
  UpdateWakeAlarmTime();
}

void Sleep::UpdateWakeAlarmTime() {
  if (lblampm != nullptr) {
    if (hourCounter.GetValue() >= 12) {
      lv_label_set_text_static(lblampm, "PM");
    } else {
      lv_label_set_text_static(lblampm, "AM");
    }
  }
  infiniSleepController.SetWakeAlarmTime(hourCounter.GetValue(), minuteCounter.GetValue());
}

void Sleep::SetAlerting() {
  lv_obj_set_hidden(enableSwitch, true);
  lv_obj_set_hidden(btnStop, false);
  taskStopWakeAlarm = lv_task_create(StopAlarmTaskCallback, pdMS_TO_TICKS(60 * 1000), LV_TASK_PRIO_MID, this);
  motorController.StartRinging();
  wakeLock.Lock();
}

void Sleep::StopAlerting() {
  infiniSleepController.StopAlerting();
  motorController.StopRinging();
  SetSwitchState(LV_ANIM_OFF);
  if (taskStopWakeAlarm != nullptr) {
    lv_task_del(taskStopWakeAlarm);
    taskStopWakeAlarm = nullptr;
  }
  wakeLock.Release();
  lv_obj_set_hidden(enableSwitch, false);
  lv_obj_set_hidden(btnStop, true);
}

void Sleep::SetSwitchState(lv_anim_enable_t anim) {
  if (infiniSleepController.IsEnabled()) {
    lv_switch_on(enableSwitch, anim);
  } else {
    lv_switch_off(enableSwitch, anim);
  }
}

void Sleep::ShowAlarmInfo() {
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

  if (infiniSleepController.IsEnabled()) {
    auto timeToAlarm = infiniSleepController.SecondsToWakeAlarm();

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

void Sleep::HideAlarmInfo() {
  lv_obj_del(btnMessage);
  txtMessage = nullptr;
  btnMessage = nullptr;
}

void Sleep::SetRecurButtonState() {
  using Pinetime::Controllers::AlarmController;
  switch (infiniSleepController.Recurrence()) {
    case InfiniSleepController::RecurType::None:
      lv_label_set_text_static(txtRecur, "ONCE");
      break;
    case InfiniSleepController::RecurType::Daily:
      lv_label_set_text_static(txtRecur, "DAILY");
      break;
    case InfiniSleepController::RecurType::Weekdays:
      lv_label_set_text_static(txtRecur, "MON-FRI");
  }
}

void Sleep::ToggleRecurrence() {
  using Pinetime::Controllers::AlarmController;
  switch (infiniSleepController.Recurrence()) {
    case InfiniSleepController::RecurType::None:
      infiniSleepController.SetRecurrence(InfiniSleepController::RecurType::Daily);
      break;
    case InfiniSleepController::RecurType::Daily:
      infiniSleepController.SetRecurrence(InfiniSleepController::RecurType::Weekdays);
      break;
    case InfiniSleepController::RecurType::Weekdays:
      infiniSleepController.SetRecurrence(InfiniSleepController::RecurType::None);
  }
  SetRecurButtonState();
}