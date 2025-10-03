#include "displayapp/screens/Timer.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/InfiniTimeTheme.h"
#include <lvgl/lvgl.h>

using namespace Pinetime::Applications::Screens;

static void btnEventHandler(lv_obj_t* obj, lv_event_t event) {
  auto* screen = static_cast<Timer*>(obj->user_data);
  if (screen->launcherMode && event == LV_EVENT_CLICKED) {
    screen->OnLauncherButtonClicked(obj);
  } else if (event == LV_EVENT_PRESSED) {
    screen->ButtonPressed();
  } else if (event == LV_EVENT_RELEASED || event == LV_EVENT_PRESS_LOST) {
    screen->MaskReset();
  } else if (event == LV_EVENT_SHORT_CLICKED) {
    screen->ToggleRunning();
  }
}

Timer::Timer(Controllers::Timer& timerController, Controllers::MotorController& motorController, Controllers::Settings& settingsController)
  : timer {timerController}, motorController {motorController}, settingsController {settingsController} {

  // If timer is already running or ringing, skip launcher and go directly to timer UI
  if (motorController.IsRinging() || timer.IsRunning()) {
    uint32_t durationMs = settingsController.GetLastTimerDuration(0);
    CreateTimerUI(durationMs, false);
  } else {
    CreateLauncherUI();
  }

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

Timer::~Timer() {
  lv_task_del(taskRefresh);
  if (launcherMode) {
    lv_style_reset(&btnStyle);
  }
  lv_obj_clean(lv_scr_act());
}

void Timer::ButtonPressed() {
  pressTime = xTaskGetTickCount();
  buttonPressing = true;
}

void Timer::MaskReset() {
  buttonPressing = false;
  // A click event is processed before a release event,
  // so the release event would override the "Pause" text without this check
  if (!timer.IsRunning()) {
    lv_label_set_text_static(txtPlayPause, "Start");
  }
  maskPosition = 0;
  UpdateMask();
}

void Timer::UpdateMask() {
  lv_draw_mask_line_param_t maskLine;

  lv_draw_mask_line_points_init(&maskLine, maskPosition, 0, maskPosition, 240, LV_DRAW_MASK_LINE_SIDE_LEFT);
  lv_objmask_update_mask(highlightObjectMask, highlightMask, &maskLine);

  lv_draw_mask_line_points_init(&maskLine, maskPosition, 0, maskPosition, 240, LV_DRAW_MASK_LINE_SIDE_RIGHT);
  lv_objmask_update_mask(btnObjectMask, btnMask, &maskLine);
}

void Timer::Refresh() {
  // Don't try to update timer display if we're in launcher mode (counters don't exist)
  if (launcherMode) {
    // If timer starts ringing while in launcher, transition to timer UI
    if (motorController.IsRinging() || timer.IsRunning()) {
      uint32_t durationMs = settingsController.GetLastTimerDuration(0);
      lv_style_reset(&btnStyle);
      lv_obj_clean(lv_scr_act());
      CreateTimerUI(durationMs, false);
    }
    return;
  }

  if (isRinging) {
    DisplayTime();
    // Stop buzzing after 10 seconds, but continue the counter
    if (motorController.IsRinging() && displaySeconds.Get().count() > 10) {
      motorController.StopRinging();
    }
    // Reset timer after 1 minute
    if (displaySeconds.Get().count() > 60) {
      Reset();
    }
  } else if (timer.IsRunning()) {
    DisplayTime();
  } else if (buttonPressing && xTaskGetTickCount() - pressTime > pdMS_TO_TICKS(150)) {
    lv_label_set_text_static(txtPlayPause, "Reset");
    maskPosition += 15;
    if (maskPosition > 240) {
      MaskReset();
      Reset();
    } else {
      UpdateMask();
    }
  }
}

void Timer::DisplayTime() {
  displaySeconds = std::chrono::duration_cast<std::chrono::seconds>(timer.GetTimeRemaining());
  if (displaySeconds.IsUpdated()) {
    minuteCounter.SetValue(displaySeconds.Get().count() / 60);
    secondCounter.SetValue(displaySeconds.Get().count() % 60);
  }
}

void Timer::SetTimerRunning() {
  if (launcherMode) {
    return;
  }
  minuteCounter.HideControls();
  secondCounter.HideControls();
  lv_label_set_text_static(txtPlayPause, "Pause");
  lv_obj_set_style_local_bg_color(btnPlayPause, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
}

void Timer::SetTimerStopped() {
  if (launcherMode) {
    isRinging = false;
    return;
  }
  isRinging = false;
  minuteCounter.ShowControls();
  secondCounter.ShowControls();
  lv_label_set_text_static(txtPlayPause, "Start");
  lv_obj_set_style_local_bg_color(btnPlayPause, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
}

void Timer::SetTimerRinging() {
  isRinging = true;
  if (launcherMode) {
    // Timer expired while in launcher mode - transition will happen in Refresh()
    return;
  }
  minuteCounter.HideControls();
  secondCounter.HideControls();
  lv_label_set_text_static(txtPlayPause, "Reset");
  lv_obj_set_style_local_bg_color(btnPlayPause, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  timer.SetExpiredTime();
}

void Timer::ToggleRunning() {
  if (isRinging) {
    motorController.StopRinging();
    Reset();
  } else if (timer.IsRunning()) {
    DisplayTime();
    timer.StopTimer();
    SetTimerStopped();
  } else if (secondCounter.GetValue() + minuteCounter.GetValue() > 0) {
    auto timerDuration = std::chrono::minutes(minuteCounter.GetValue()) + std::chrono::seconds(secondCounter.GetValue());
    timer.StartTimer(timerDuration);

    // Save the timer duration to MRU list
    uint32_t durationMs = (minuteCounter.GetValue() * 60 + secondCounter.GetValue()) * 1000;
    settingsController.AddTimerDuration(durationMs);

    Refresh();
    SetTimerRunning();
  }
}

void Timer::Reset() {
  timer.ResetExpiredTime();
  DisplayTime();
  SetTimerStopped();
}

void Timer::CreateLauncherUI() {
  static constexpr uint8_t innerDistance = 10;
  static constexpr uint8_t buttonHeight = (LV_VER_RES_MAX - innerDistance) / 2;
  static constexpr uint8_t buttonWidth = (LV_HOR_RES_MAX - innerDistance) / 2;
  static constexpr uint8_t buttonXOffset = (LV_HOR_RES_MAX - buttonWidth * 2 - innerDistance) / 2;

  lv_style_init(&btnStyle);
  lv_style_set_radius(&btnStyle, LV_STATE_DEFAULT, buttonHeight / 4);
  lv_style_set_bg_color(&btnStyle, LV_STATE_DEFAULT, Colors::bgAlt);

  // Top-left: Most recent timer
  btnRecent1 = lv_btn_create(lv_scr_act(), nullptr);
  btnRecent1->user_data = this;
  lv_obj_set_event_cb(btnRecent1, btnEventHandler);
  lv_obj_add_style(btnRecent1, LV_BTN_PART_MAIN, &btnStyle);
  lv_obj_set_size(btnRecent1, buttonWidth, buttonHeight);
  lv_obj_align(btnRecent1, nullptr, LV_ALIGN_IN_TOP_LEFT, buttonXOffset, 0);

  uint32_t duration1 = settingsController.GetLastTimerDuration(0);
  uint32_t minutes1 = duration1 / 60000;
  uint32_t seconds1 = (duration1 % 60000) / 1000;

  labelRecent1 = lv_label_create(btnRecent1, nullptr);
  lv_obj_t* labelRecent1Icon = lv_label_create(btnRecent1, nullptr);

  if (seconds1 == 0) {
    // Show just minutes
    lv_obj_set_style_local_text_font(labelRecent1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
    lv_label_set_text_fmt(labelRecent1, "%lu", minutes1);
    lv_obj_align(labelRecent1, btnRecent1, LV_ALIGN_CENTER, 0, -20);

    // Show "min" below
    lv_obj_set_style_local_text_font(labelRecent1Icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_static(labelRecent1Icon, "min");
    lv_obj_align(labelRecent1Icon, btnRecent1, LV_ALIGN_CENTER, 0, 20);
  } else {
    // Show MM
    lv_obj_set_style_local_text_font(labelRecent1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
    lv_label_set_text_fmt(labelRecent1, "%lu", minutes1);
    lv_obj_align(labelRecent1, btnRecent1, LV_ALIGN_CENTER, 0, -30);

    // Show :SS below
    lv_obj_set_style_local_text_font(labelRecent1Icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_fmt(labelRecent1Icon, ":%02lu", seconds1);
    lv_obj_align(labelRecent1Icon, btnRecent1, LV_ALIGN_CENTER, 0, 30);
  }

  // Top-right: 2nd recent timer
  btnRecent2 = lv_btn_create(lv_scr_act(), nullptr);
  btnRecent2->user_data = this;
  lv_obj_set_event_cb(btnRecent2, btnEventHandler);
  lv_obj_add_style(btnRecent2, LV_BTN_PART_MAIN, &btnStyle);
  lv_obj_set_size(btnRecent2, buttonWidth, buttonHeight);
  lv_obj_align(btnRecent2, nullptr, LV_ALIGN_IN_TOP_RIGHT, -buttonXOffset, 0);

  uint32_t duration2 = settingsController.GetLastTimerDuration(1);
  uint32_t minutes2 = duration2 / 60000;
  uint32_t seconds2 = (duration2 % 60000) / 1000;

  labelRecent2 = lv_label_create(btnRecent2, nullptr);
  lv_obj_t* labelRecent2Icon = lv_label_create(btnRecent2, nullptr);

  if (seconds2 == 0) {
    // Show just minutes
    lv_obj_set_style_local_text_font(labelRecent2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
    lv_label_set_text_fmt(labelRecent2, "%lu", minutes2);
    lv_obj_align(labelRecent2, btnRecent2, LV_ALIGN_CENTER, 0, -20);

    // Show "min" below
    lv_obj_set_style_local_text_font(labelRecent2Icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_static(labelRecent2Icon, "min");
    lv_obj_align(labelRecent2Icon, btnRecent2, LV_ALIGN_CENTER, 0, 20);
  } else {
    // Show MM
    lv_obj_set_style_local_text_font(labelRecent2, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
    lv_label_set_text_fmt(labelRecent2, "%lu", minutes2);
    lv_obj_align(labelRecent2, btnRecent2, LV_ALIGN_CENTER, 0, -30);

    // Show :SS below
    lv_obj_set_style_local_text_font(labelRecent2Icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_fmt(labelRecent2Icon, ":%02lu", seconds2);
    lv_obj_align(labelRecent2Icon, btnRecent2, LV_ALIGN_CENTER, 0, 30);
  }

  // Bottom-left: 3rd recent timer
  btnRecent3 = lv_btn_create(lv_scr_act(), nullptr);
  btnRecent3->user_data = this;
  lv_obj_set_event_cb(btnRecent3, btnEventHandler);
  lv_obj_add_style(btnRecent3, LV_BTN_PART_MAIN, &btnStyle);
  lv_obj_set_size(btnRecent3, buttonWidth, buttonHeight);
  lv_obj_align(btnRecent3, nullptr, LV_ALIGN_IN_BOTTOM_LEFT, buttonXOffset, 0);

  uint32_t duration3 = settingsController.GetLastTimerDuration(2);
  uint32_t minutes3 = duration3 / 60000;
  uint32_t seconds3 = (duration3 % 60000) / 1000;

  labelRecent3 = lv_label_create(btnRecent3, nullptr);
  lv_obj_t* labelRecent3Icon = lv_label_create(btnRecent3, nullptr);

  if (seconds3 == 0) {
    // Show just minutes
    lv_obj_set_style_local_text_font(labelRecent3, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
    lv_label_set_text_fmt(labelRecent3, "%lu", minutes3);
    lv_obj_align(labelRecent3, btnRecent3, LV_ALIGN_CENTER, 0, -20);

    // Show "min" below
    lv_obj_set_style_local_text_font(labelRecent3Icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_static(labelRecent3Icon, "min");
    lv_obj_align(labelRecent3Icon, btnRecent3, LV_ALIGN_CENTER, 0, 20);
  } else {
    // Show MM
    lv_obj_set_style_local_text_font(labelRecent3, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
    lv_label_set_text_fmt(labelRecent3, "%lu", minutes3);
    lv_obj_align(labelRecent3, btnRecent3, LV_ALIGN_CENTER, 0, -30);

    // Show :SS below
    lv_obj_set_style_local_text_font(labelRecent3Icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_label_set_text_fmt(labelRecent3Icon, ":%02lu", seconds3);
    lv_obj_align(labelRecent3Icon, btnRecent3, LV_ALIGN_CENTER, 0, 30);
  }

  // Bottom-right: Custom timer
  btnCustom = lv_btn_create(lv_scr_act(), nullptr);
  btnCustom->user_data = this;
  lv_obj_set_event_cb(btnCustom, btnEventHandler);
  lv_obj_add_style(btnCustom, LV_BTN_PART_MAIN, &btnStyle);
  lv_obj_set_size(btnCustom, buttonWidth, buttonHeight);
  lv_obj_align(btnCustom, nullptr, LV_ALIGN_IN_BOTTOM_RIGHT, -buttonXOffset, 0);

  labelCustom = lv_label_create(btnCustom, nullptr);
  lv_obj_set_style_local_text_font(labelCustom, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &lv_font_sys_48);
  lv_label_set_text_static(labelCustom, Symbols::hourGlass);
}

void Timer::CreateTimerUI(uint32_t startDurationMs, bool autoStart) {
  launcherMode = false;

  lv_obj_t* colonLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(colonLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
  lv_obj_set_style_local_text_color(colonLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(colonLabel, ":");
  lv_obj_align(colonLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, -29);

  minuteCounter.Create();
  secondCounter.Create();
  lv_obj_align(minuteCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_obj_align(secondCounter.GetObject(), nullptr, LV_ALIGN_IN_TOP_RIGHT, 0, 0);

  highlightObjectMask = lv_objmask_create(lv_scr_act(), nullptr);
  lv_obj_set_size(highlightObjectMask, 240, 50);
  lv_obj_align(highlightObjectMask, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  lv_draw_mask_line_param_t tmpMaskLine;

  lv_draw_mask_line_points_init(&tmpMaskLine, 0, 0, 0, 240, LV_DRAW_MASK_LINE_SIDE_LEFT);
  highlightMask = lv_objmask_add_mask(highlightObjectMask, &tmpMaskLine);

  lv_obj_t* btnHighlight = lv_obj_create(highlightObjectMask, nullptr);
  lv_obj_set_style_local_radius(btnHighlight, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_color(btnHighlight, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_ORANGE);
  lv_obj_set_size(btnHighlight, LV_HOR_RES, 50);
  lv_obj_align(btnHighlight, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  btnObjectMask = lv_objmask_create(lv_scr_act(), nullptr);
  lv_obj_set_size(btnObjectMask, 240, 50);
  lv_obj_align(btnObjectMask, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  lv_draw_mask_line_points_init(&tmpMaskLine, 0, 0, 0, 240, LV_DRAW_MASK_LINE_SIDE_RIGHT);
  btnMask = lv_objmask_add_mask(btnObjectMask, &tmpMaskLine);

  btnPlayPause = lv_btn_create(btnObjectMask, nullptr);
  btnPlayPause->user_data = this;
  lv_obj_set_style_local_radius(btnPlayPause, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_color(btnPlayPause, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_event_cb(btnPlayPause, btnEventHandler);
  lv_obj_set_size(btnPlayPause, LV_HOR_RES, 50);

  // Create the label as a child of the button so it stays centered by default
  txtPlayPause = lv_label_create(btnPlayPause, nullptr);

  // Reset button press state
  buttonPressing = false;
  pressTime = 0;

  if (motorController.IsRinging()) {
    SetTimerRinging();
    DisplayTime();
  } else if (timer.IsRunning()) {
    SetTimerRunning();
    DisplayTime();
  } else if (autoStart) {
    auto timerDuration = std::chrono::milliseconds(startDurationMs);
    timer.StartTimer(timerDuration);
    settingsController.AddTimerDuration(startDurationMs);
    SetTimerRunning();
    DisplayTime();
  } else {
    // Set the initial duration only when timer is stopped
    uint32_t minutes = startDurationMs / 60000;
    uint32_t seconds = (startDurationMs % 60000) / 1000;
    minuteCounter.SetValue(minutes);
    secondCounter.SetValue(seconds);
    SetTimerStopped();
  }
}

void Timer::OnLauncherButtonClicked(lv_obj_t* obj) {
  uint32_t durationMs;
  bool autoStart;

  if (obj == btnRecent1) {
    durationMs = settingsController.GetLastTimerDuration(0);
    autoStart = true;
  } else if (obj == btnRecent2) {
    durationMs = settingsController.GetLastTimerDuration(1);
    autoStart = true;
  } else if (obj == btnRecent3) {
    durationMs = settingsController.GetLastTimerDuration(2);
    autoStart = true;
  } else if (obj == btnCustom) {
    durationMs = 0;
    autoStart = false;
  } else {
    return;
  }

  lv_style_reset(&btnStyle);
  lv_obj_clean(lv_scr_act());

  CreateTimerUI(durationMs, autoStart);

  // Wait for button release to prevent the press state from carrying over to the new UI
  lv_indev_wait_release(lv_indev_get_act());
}
