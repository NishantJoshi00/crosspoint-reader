#include "ArcActivity.h"

#include <ArcGraphics.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* GAME_DIRECTORY = "/games/arc";
using Button = MappedInputManager::Button;
}  // namespace

void ArcActivity::onEnter() {
  Activity::onEnter();
  if (!session.start()) {
    error = session.error();
    screen = Screen::Error;
  } else {
    scanGames();
  }
  requestUpdate();
}

void ArcActivity::onExit() {
  session.shutdown();
  Activity::onExit();
}

void ArcActivity::scanGames() {
  files.clear();
  if (!Storage.ready()) {
    error = tr(STR_ARC_NO_SD);
    screen = Screen::Error;
    return;
  }
  HalFile directory = Storage.open(GAME_DIRECTORY);
  if (directory && directory.isDirectory()) {
    while (files.size() < 128) {
      HalFile file = directory.openNextFile();
      if (!file) break;
      char name[224] = {};
      if (!file.getName(name, sizeof(name))) continue;
      const size_t length = std::strlen(name);
      if (!file.isDirectory() && length > 4 && std::strcmp(name + length - 4, ".bin") == 0) files.emplace_back(name);
    }
  }
  std::sort(files.begin(), files.end());
  gameSelection = files.empty() ? 0 : std::min(gameSelection, static_cast<int>(files.size()) - 1);
  selection = gameSelection;
}

std::string ArcActivity::progressPath() const {
  return std::string(GAME_DIRECTORY) + "/." + files[gameSelection] + ".level";
}

unsigned ArcActivity::savedLevel() const {
  char value[16] = {};
  Storage.readFileToBuffer(progressPath().c_str(), value, sizeof(value));
  char* end;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  return end != value && parsed < 256 ? static_cast<unsigned>(parsed) : 0;
}

void ArcActivity::saveLevel() const {
  char value[16];
  std::snprintf(value, sizeof(value), "%u\n", level);
  if (!Storage.writeFile(progressPath().c_str(), String(value))) LOG_ERR("ARC", "Cannot save level to SD card");
}

void ArcActivity::openGame(unsigned startLevel) {
  if (files.empty()) return;
  ArcSession::Command command;
  command.operation = ArcSession::Operation::Open;
  command.level = startLevel;
  std::snprintf(command.path, sizeof(command.path), "%s/%s", GAME_DIRECTORY, files[gameSelection].c_str());
  if (!session.submit(command)) return;
  operation = command.operation;
  waiting = true;
  title = files[gameSelection].substr(0, files[gameSelection].find('-'));
  for (char& character : title) character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  screen = Screen::Loading;
  cursorX = cursorY = 31;
  confirmHeld = false;
  requestUpdate();
}

void ArcActivity::sendAction(unsigned id) {
  ArcSession::Command command;
  command.operation = ArcSession::Operation::Action;
  command.action = id;
  command.x = cursorX;
  command.y = cursorY;
  if (!session.submit(command)) return;
  operation = command.operation;
  waiting = true;
}

void ArcActivity::completed() {
  waiting = false;
  if (!session.succeeded()) {
    error = session.error();
    screen = Screen::Error;
  } else if (operation == ArcSession::Operation::Close) {
    screen = Screen::Games;
    scanGames();
  } else {
    const auto& player = session.player();
    const bool changedLevel = level != player.level();
    level = player.level();
    levels = player.levels();
    actions = player.actions();
    state = player.state();
    moves = player.moves();
    std::memcpy(frame, player.frame(), sizeof(frame));
    if (operation == ArcSession::Operation::Open) cursorMode = (actions & (1 << 6)) && !(actions & 0x1e);
    if (operation == ArcSession::Operation::Open || changedLevel) saveLevel();
    screen = Screen::Play;
  }
  requestUpdate();
}

void ArcActivity::showMenu() {
  menu = {MenuAction::Resume};
  if ((actions & (1 << 6)) && (actions & 0x1e)) menu.push_back(MenuAction::Cursor);
  if (actions & (1 << 5)) menu.push_back(MenuAction::Action5);
  if (actions & (1 << 7)) menu.push_back(MenuAction::Undo);
  menu.push_back(MenuAction::Reset);
  menu.push_back(MenuAction::Levels);
  menu.push_back(MenuAction::Games);
  selection = 0;
  screen = Screen::Menu;
  requestUpdate();
}

std::string ArcActivity::menuLabel(int index) const {
  switch (menu[index]) {
    case MenuAction::Resume:
      return tr(STR_ARC_RESUME);
    case MenuAction::Cursor:
      return cursorMode ? tr(STR_ARC_MOVE_MODE) : tr(STR_ARC_CURSOR_MODE);
    case MenuAction::Action5:
      return tr(STR_ARC_ACTION5);
    case MenuAction::Undo:
      return tr(STR_ARC_UNDO);
    case MenuAction::Reset:
      return tr(STR_ARC_RESET);
    case MenuAction::Levels:
      return tr(STR_ARC_LEVELS);
    case MenuAction::Games:
      return tr(STR_ARC_GAMES);
  }
  return {};
}

void ArcActivity::activate() {
  if (screen == Screen::Games && !files.empty()) {
    gameSelection = selection;
    openGame(savedLevel());
  } else if (screen == Screen::Levels) {
    openGame(selection);
  } else if (screen == Screen::Menu) {
    switch (menu[selection]) {
      case MenuAction::Resume:
        screen = Screen::Play;
        break;
      case MenuAction::Cursor:
        cursorMode = !cursorMode;
        screen = Screen::Play;
        break;
      case MenuAction::Action5:
        screen = Screen::Play;
        sendAction(5);
        break;
      case MenuAction::Undo:
        screen = Screen::Play;
        sendAction(7);
        break;
      case MenuAction::Reset:
        openGame(level);
        break;
      case MenuAction::Levels:
        screen = Screen::Levels;
        selection = level;
        break;
      case MenuAction::Games: {
        ArcSession::Command command;
        if (session.submit(command)) {
          operation = command.operation;
          waiting = true;
          screen = Screen::Loading;
        }
        break;
      }
    }
    requestUpdate();
  }
}

void ArcActivity::move(int dx, int dy, unsigned action) {
  if (cursorMode) {
    const int step = mappedInput.getHeldTime() > 1000 ? 4 : 1;
    cursorX = std::clamp(static_cast<int>(cursorX) + dx * step, 0, 63);
    cursorY = std::clamp(static_cast<int>(cursorY) + dy * step, 0, 63);
    requestUpdate();
  } else if (actions & (1 << action)) {
    sendAction(action);
  }
}

void ArcActivity::loop() {
  RenderLock lock;
  if (waiting) {
    if (session.busy()) {
      if (mappedInput.wasReleased(Button::Back)) session.cancel();
      return;
    }
    completed();
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    if (screen == Screen::Games || screen == Screen::Error) {
      lock.unlock();
      activityManager.goHome(HomeMenuItem::ARC);
    } else if (screen == Screen::Play) {
      showMenu();
    } else {
      screen = Screen::Play;
      requestUpdate();
    }
    return;
  }
  if (screen == Screen::Error) return;
  if (screen == Screen::Play) {
    if ((actions & (1 << 6)) && (actions & 0x1e) && mappedInput.isPressed(Button::Confirm) &&
        mappedInput.getHeldTime() >= 650 && !confirmHeld) {
      cursorMode = !cursorMode;
      confirmHeld = true;
      requestUpdate();
    }
    if (mappedInput.wasReleased(Button::Confirm)) {
      if (confirmHeld)
        confirmHeld = false;
      else if (state)
        showMenu();
      else if (cursorMode)
        sendAction(6);
      else if (actions & (1 << 5))
        sendAction(5);
      else
        showMenu();
      return;
    }
    if (!state) {
      navigator.onPressAndContinuous({Button::Left}, [this] {
        if (!waiting) move(-1, 0, 3);
      });
      navigator.onPressAndContinuous({Button::Right}, [this] {
        if (!waiting) move(1, 0, 4);
      });
      navigator.onPressAndContinuous({Button::Up}, [this] {
        if (!waiting) move(0, -1, 1);
      });
      navigator.onPressAndContinuous({Button::Down}, [this] {
        if (!waiting) move(0, 1, 2);
      });
      int x, y;
      const int cell = cellSize(), left = (renderer.getScreenWidth() - 64 * cell) / 2, top = boardTop();
      if (!waiting && (actions & (1 << 6)) && mappedInput.wasScreenTapped(x, y) && x >= left && y >= top &&
          x < left + cell * 64 && y < top + cell * 64) {
        cursorX = (x - left) / cell;
        cursorY = (y - top) / cell;
        sendAction(6);
      }
    }
    return;
  }
  const int count = screen == Screen::Games ? files.size() : screen == Screen::Levels ? levels : menu.size();
  if (count == 0) return;
  if (mappedInput.wasReleased(Button::Confirm)) {
    activate();
    return;
  }
  navigator.onNext([this, count] {
    selection = ButtonNavigator::nextIndex(selection, count);
    requestUpdate();
  });
  navigator.onPrevious([this, count] {
    selection = ButtonNavigator::previousIndex(selection, count);
    requestUpdate();
  });
}

int ArcActivity::cellSize() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int footer = 80 + metrics.buttonHintsHeight + metrics.verticalSpacing;
  return std::max(
      1, std::min((renderer.getScreenWidth() - 16) / 64, (renderer.getScreenHeight() - boardTop() - footer) / 64));
}

int ArcActivity::boardTop() const { return UITheme::getInstance().getMetrics().topPadding + 100; }

void ArcActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth(), height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight},
                 screen == Screen::Games ? tr(STR_ARC_APP)
                 : title.empty()         ? tr(STR_ARC_APP)
                                         : title.c_str());
  if (screen == Screen::Play) {
    char status[80];
    std::snprintf(status, sizeof(status), tr(STR_ARC_STATUS), level + 1, levels, moves);
    renderer.drawCenteredText(UI_10_FONT_ID, boardTop() - 28, status);
    const int cell = cellSize(), left = (width - 64 * cell) / 2, top = boardTop();
    for (int y = 0; y < 64 * cell; ++y) {
      for (int x = 0; x < 64 * cell; ++x) {
        if (arc::ink(frame[(y / cell) * 64 + x / cell], (x % cell) * 8 / cell, (y % cell) * 8 / cell))
          renderer.drawPixel(left + x, top + y);
      }
    }
    renderer.drawRect(left - 1, top - 1, cell * 64 + 2, cell * 64 + 2);
    if (cursorMode && !state) {
      const int x = left + cursorX * cell, y = top + cursorY * cell;
      renderer.drawRect(x, y, cell, cell, false);
      renderer.drawRect(x - 1, y - 1, cell + 2, cell + 2, true);
    }
    const int below = top + 64 * cell + 12;
    if (state) {
      renderer.drawCenteredText(UI_12_FONT_ID, below, state == 1 ? tr(STR_ARC_WON) : tr(STR_ARC_GAME_OVER));
    } else if (cursorMode) {
      std::snprintf(status, sizeof(status), tr(STR_ARC_CURSOR_STATUS), cursorX, cursorY, frame[cursorY * 64 + cursorX]);
      renderer.drawCenteredText(UI_10_FONT_ID, below, status);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, below,
                                (actions & (1 << 6)) ? tr(STR_ARC_HOLD_CURSOR) : tr(STR_ARC_MOVE_HINT));
    }
    const int paletteTop = below + 26;
    const int slot = std::min(28, (width - 24) / 16), paletteLeft = (width - slot * 16) / 2;
    for (unsigned color = 0; color < 16; ++color) {
      const int x = paletteLeft + color * slot;
      for (int py = 0; py < 16; ++py)
        for (int px = 0; px < 16; ++px)
          if (arc::ink(color, px, py)) renderer.drawPixel(x + px, paletteTop + py);
      char label[4];
      std::snprintf(label, sizeof(label), "%X", color);
      renderer.drawText(UI_10_FONT_ID, x, paletteTop + 18, label);
    }
    const auto labels =
        mappedInput.mapLabels(tr(STR_ARC_MENU), cursorMode ? tr(STR_ARC_CLICK) : tr(STR_SELECT), "<", ">");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (screen == Screen::Loading || screen == Screen::Error || (screen == Screen::Games && files.empty())) {
    const char* text = screen == Screen::Loading ? tr(STR_ARC_LOADING)
                       : screen == Screen::Error ? error.c_str()
                                                 : tr(STR_ARC_EMPTY);
    const auto lines = renderer.wrappedText(UI_12_FONT_ID, text, width - 48, 6);
    int y = height / 2 - static_cast<int>(lines.size()) * 14;
    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
      y += 30;
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const int count = screen == Screen::Games ? files.size() : screen == Screen::Levels ? levels : menu.size();
    const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    GUI.drawList(renderer, Rect{0, top, width, height - top - metrics.buttonHintsHeight - 24}, count, selection,
                 [this](int index) {
                   if (screen == Screen::Games) return files[index].substr(0, files[index].size() - 4);
                   if (screen == Screen::Menu) return menuLabel(index);
                   char text[40];
                   std::snprintf(text, sizeof(text), tr(STR_ARC_LEVEL), index + 1);
                   return std::string(text);
                 });
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(++refreshes % 12 == 0 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
