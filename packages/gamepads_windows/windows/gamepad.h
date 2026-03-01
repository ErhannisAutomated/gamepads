#include <windows.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>

struct Gamepad {
  UINT joy_id;
  std::string name;
  int num_buttons;
  std::atomic<bool> alive{false};

  Gamepad(UINT id, std::string n, int nb)
      : joy_id(id), name(std::move(n)), num_buttons(nb), alive(true) {}
};

struct Event {
  int time;
  std::string type;
  std::string key;
  int value;
};

class Gamepads {
 private:
  std::list<Event> diff_states(Gamepad* gamepad,
                               const JOYINFOEX& old,
                               const JOYINFOEX& current);
  bool are_states_different(const JOYINFOEX& a, const JOYINFOEX& b);
  void read_gamepad(std::shared_ptr<Gamepad> gamepad);
  void connect_gamepad(UINT joy_id, std::string name, int num_buttons);

 public:
  std::map<UINT, std::shared_ptr<Gamepad>> gamepads;
  std::optional<std::function<void(Gamepad* gamepad, const Event& event)>>
      event_emitter;
  void update_gamepads();
};

extern Gamepads gamepads;

std::optional<LRESULT> CALLBACK GamepadListenerProc(HWND hwnd,
                                                    UINT uMsg,
                                                    WPARAM wParam,
                                                    LPARAM lParam);
