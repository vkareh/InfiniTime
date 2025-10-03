#include "components/settings/Settings.h"
#include <cstdlib>
#include <cstring>

using namespace Pinetime::Controllers;

Settings::Settings(Pinetime::Controllers::FS& fs) : fs {fs} {
}

void Settings::Init() {

  // Load default settings from Flash
  LoadSettingsFromFile();
}

void Settings::SaveSettings() {

  // verify if is necessary to save
  if (settingsChanged) {
    SaveSettingsToFile();
  }
  settingsChanged = false;
}

void Settings::LoadSettingsFromFile() {
  SettingsData bufferSettings;
  lfs_file_t settingsFile;

  if (fs.FileOpen(&settingsFile, "/settings.dat", LFS_O_RDONLY) != LFS_ERR_OK) {
    return;
  }
  fs.FileRead(&settingsFile, reinterpret_cast<uint8_t*>(&bufferSettings), sizeof(settings));
  fs.FileClose(&settingsFile);
  if (bufferSettings.version == settingsVersion) {
    settings = bufferSettings;
  }
}

void Settings::SaveSettingsToFile() {
  lfs_file_t settingsFile;

  if (fs.FileOpen(&settingsFile, "/settings.dat", LFS_O_WRONLY | LFS_O_CREAT) != LFS_ERR_OK) {
    return;
  }
  fs.FileWrite(&settingsFile, reinterpret_cast<uint8_t*>(&settings), sizeof(settings));
  fs.FileClose(&settingsFile);
}

void Settings::AddTimerDuration(uint32_t duration) {
  // Search for existing duration in array
  int existingIndex = -1;
  for (int i = 0; i < 3; i++) {
    if (settings.lastTimerDurations[i] == duration) {
      existingIndex = i;
      break;
    }
  }

  // If duration already exists, move it to front
  if (existingIndex >= 0) {
    if (existingIndex == 0) {
      // Already at front, no change needed
      return;
    }
    // Shift elements before existingIndex forward by one
    for (int i = existingIndex; i > 0; i--) {
      settings.lastTimerDurations[i] = settings.lastTimerDurations[i - 1];
    }
    settings.lastTimerDurations[0] = duration;
  } else {
    // New duration - shift all down and insert at front
    settings.lastTimerDurations[2] = settings.lastTimerDurations[1];
    settings.lastTimerDurations[1] = settings.lastTimerDurations[0];
    settings.lastTimerDurations[0] = duration;
  }

  settingsChanged = true;
}
