#pragma once

#include <string>

#include "common.h"

bool IsElevated();
std::wstring ExePath();
std::wstring CurrentUserName();  // DOMAIN\user

// Starting at sign-in. Normally a "Void Shell" entry under HKCU\...\Run. When Void Shell runs as administrator it
// uses a scheduled task with highest privileges instead, because a Run entry can't start an elevated app without
// a UAC prompt, and only an elevated Void Shell can create that task.
struct StartupStatus {
    bool runEntry;
    bool task;
};
StartupStatus ReadStartup();

// Brings Windows' startup entries in line with the settings. Returns a short sentence for the settings window when
// something can't be finished yet (usually: needs administrator rights), otherwise an empty string.
// Needs COM initialised on the calling thread.
std::wstring ApplyStartup(bool startAtLogin, bool runAsAdmin);

// The scheduled task's definition: runs at this user's sign-in, never times out, ignores battery state.
std::wstring StartupTaskXml(const std::wstring& exePath, const std::wstring& user);
// Asks Task Scheduler to parse the definition without registering anything.
bool ValidateTaskXml(const std::wstring& xml, std::wstring& error);

// Starts an elevated copy (it takes over once this one exits). False if the user declined the prompt.
bool RestartElevated();
