#include <iostream>
#define WIN32_LEAN_AND_MEAN
#include <initguid.h>
#include <windows.h>
#include <dbt.h>
#include <hidclass.h>
#pragma comment(lib, "winmm.lib")
#include <mmsystem.h>

#include <list>
#include <map>
#include <memory>
#include <thread>

#include "gamepad.h"
#include "utils.h"

Gamepads gamepads;

std::list<Event> Gamepads::diff_states(Gamepad* gamepad,
                                       const JOYINFOEX& old,
                                       const JOYINFOEX& current) {
  std::time_t now = std::time(nullptr);
  int time = static_cast<int>(now);

  std::list<Event> events;
  if (old.dwXpos != current.dwXpos) {
    events.push_back(
        {time, "analog", "dwXpos", static_cast<int>(current.dwXpos)});
  }
  if (old.dwYpos != current.dwYpos) {
    events.push_back(
        {time, "analog", "dwYpos", static_cast<int>(current.dwYpos)});
  }
  if (old.dwZpos != current.dwZpos) {
    events.push_back(
        {time, "analog", "dwZpos", static_cast<int>(current.dwZpos)});
  }
  if (old.dwRpos != current.dwRpos) {
    events.push_back(
        {time, "analog", "dwRpos", static_cast<int>(current.dwRpos)});
  }
  if (old.dwUpos != current.dwUpos) {
    events.push_back(
        {time, "analog", "dwUpos", static_cast<int>(current.dwUpos)});
  }
  if (old.dwVpos != current.dwVpos) {
    events.push_back(
        {time, "analog", "dwVpos", static_cast<int>(current.dwVpos)});
  }
  if (old.dwPOV != current.dwPOV) {
    events.push_back({time, "analog", "pov", static_cast<int>(current.dwPOV)});
  }
  if (old.dwButtons != current.dwButtons) {
    for (int i = 0; i < gamepad->num_buttons; ++i) {
      bool was_pressed = old.dwButtons & (1 << i);
      bool is_pressed = current.dwButtons & (1 << i);
      if (was_pressed != is_pressed) {
        events.push_back(
            {time, "button", "button-" + std::to_string(i), is_pressed});
      }
    }
  }
  return events;
}

bool Gamepads::are_states_different(const JOYINFOEX& a, const JOYINFOEX& b) {
  return a.dwXpos != b.dwXpos || a.dwYpos != b.dwYpos || a.dwZpos != b.dwZpos ||
         a.dwRpos != b.dwRpos || a.dwUpos != b.dwUpos || a.dwVpos != b.dwVpos ||
         a.dwButtons != b.dwButtons || a.dwPOV != b.dwPOV;
}

void Gamepads::read_gamepad(std::shared_ptr<Gamepad> gamepad) {
  JOYINFOEX state;
  state.dwSize = sizeof(JOYINFOEX);
  state.dwFlags = JOY_RETURNALL;

  int joy_id = gamepad->joy_id;

  std::cout << "Listening to gamepad " << joy_id << std::endl;

  while (gamepad->alive) {
    JOYINFOEX previous_state = state;
    MMRESULT result = joyGetPosEx(joy_id, &state);
    if (result == JOYERR_NOERROR) {
      if (are_states_different(previous_state, state)) {
        std::list<Event> events = diff_states(gamepad.get(), previous_state, state);
        for (auto joy_event : events) {
          if (event_emitter.has_value()) {
            (*event_emitter)(gamepad.get(), joy_event);
          }
        }
      }
    } else {
      std::cout << "Fail to listen to gamepad " << joy_id << std::endl;
      gamepad->alive = false;
    }
    Sleep(1);
  }
}

void Gamepads::connect_gamepad(UINT joy_id, std::string name, int num_buttons) {
  auto gamepad = std::make_shared<Gamepad>(joy_id, name, num_buttons);
  gamepads[joy_id] = gamepad;
  std::thread read_thread([this, gamepad]() { read_gamepad(gamepad); });
  read_thread.detach();
}

void Gamepads::update_gamepads() {
  std::cout << "Updating gamepads..." << std::endl;

  // Clean up entries whose polling threads have exited due to disconnection.
  for (auto it = gamepads.begin(); it != gamepads.end(); ) {
    if (!it->second->alive) {
      it = gamepads.erase(it);
    } else {
      ++it;
    }
  }

  UINT max_joysticks = joyGetNumDevs();
  JOYCAPSW joy_caps;
  for (UINT joy_id = 0; joy_id < max_joysticks; ++joy_id) {
    MMRESULT result = joyGetDevCapsW(joy_id, &joy_caps, sizeof(JOYCAPSW));
    if (result == JOYERR_NOERROR) {
      std::string name = to_string(joy_caps.szPname);
      int num_buttons = static_cast<int>(joy_caps.wNumButtons);
      auto it = gamepads.find(joy_id);
      if (it != gamepads.end()) {
        if (it->second->name != name) {
          std::cout << "Updated gamepad " << joy_id << std::endl;
          it->second->alive = false;
          gamepads.erase(it);
          connect_gamepad(joy_id, name, num_buttons);
        }
      } else {
        std::cout << "New gamepad connected " << joy_id << std::endl;
        connect_gamepad(joy_id, name, num_buttons);
      }
    }
  }
}

// A private message used to defer update_gamepads() to the next message loop
// iteration. Using PostMessage instead of SetTimer avoids conflicts with any
// timers (Flutter's or otherwise) that may use the same window and timer ID.
static const UINT WM_GAMEPADS_UPDATE = WM_APP + 0x67;
static bool gamepads_update_pending = false;

std::optional<LRESULT> CALLBACK GamepadListenerProc(HWND hwnd,
                                                    UINT uMsg,
                                                    WPARAM wParam,
                                                    LPARAM lParam) {
  // Handle our private deferred-update message.
  if (uMsg == WM_GAMEPADS_UPDATE) {
    gamepads_update_pending = false;
    gamepads.update_gamepads();
    return 0;
  }

  switch (uMsg) {
    case WM_DEVICECHANGE: {
      if (lParam != NULL) {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
        if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
          PDEV_BROADCAST_DEVICEINTERFACE pDevInterface =
              (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
          if (IsEqualGUID(pDevInterface->dbcc_classguid,
                          GUID_DEVINTERFACE_HID)) {
            if (wParam == DBT_DEVICEARRIVAL ||
                wParam == DBT_DEVICEREMOVECOMPLETE) {
              // Debounce: a single physical device generates one
              // WM_DEVICECHANGE per HID interface it exposes. Post a single
              // deferred update message; the pending flag prevents duplicates.
              if (!gamepads_update_pending) {
                gamepads_update_pending = true;
                PostMessage(hwnd, WM_GAMEPADS_UPDATE, 0, 0);
              }
            }
          }
        }
      }
      return 0;
    }
    case WM_DESTROY: {
      PostQuitMessage(0);
      return 0;
    }
  }
  return std::nullopt;
}
