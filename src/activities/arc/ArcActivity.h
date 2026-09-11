#pragma once

#include <vector>

#include "ArcSession.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ArcActivity final : public Activity {
 public:
  ArcActivity(GfxRenderer& renderer, MappedInputManager& input) : Activity("ARC", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return session.busy(); }

 private:
  enum class Screen : uint8_t { Games, Loading, Play, Menu, Levels, Error };
  enum class MenuAction : uint8_t { Resume, Cursor, Action5, Undo, Reset, Levels, Games };
  ArcSession session;
  ButtonNavigator navigator{160, 550};
  Screen screen = Screen::Games;
  ArcSession::Operation operation = ArcSession::Operation::Close;
  std::vector<std::string> files;
  std::vector<MenuAction> menu;
  int selection = 0, gameSelection = 0;
  unsigned cursorX = 31, cursorY = 31;
  unsigned level = 0, levels = 0, actions = 0, state = 0, moves = 0;
  unsigned refreshes = 0;
  bool waiting = false, cursorMode = false, confirmHeld = false;
  uint8_t frame[4096] = {};
  std::string error, title;

  void scanGames();
  void openGame(unsigned startLevel);
  void sendAction(unsigned id);
  void showMenu();
  void activate();
  void move(int dx, int dy, unsigned action);
  void completed();
  std::string menuLabel(int index) const;
  int cellSize() const;
  int boardTop() const;
  std::string progressPath() const;
  unsigned savedLevel() const;
  void saveLevel() const;
};
