#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/uart.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "app_logger.h"
#include "battery_gauge.h"
#include "board_pins.h"
#include "driver.h"
#include "dither.h"
#include "double_tap_tracker.h"
#include "e1005_fast_refresh.h"
#include "epaper_setup.h"
#include "epub_archive.h"
#include "epub_browser_logic.h"
#include "epub_cover.h"
#include "epub_latin_fonts.h"
#include "epub_text.h"
#include "game_help_text.h"
#include "game_language_store.h"
#include "config_portal.h"
#include "wifi_schema.h"
#include "config_portal_ui.h"
#include "dashboard_render.h"
#include "sticky_wifi_credentials.h"
#include "game_localization.h"
#include "game_progress_store.h"
#include "game_ui_fonts.h"
#include "gt911_touch.h"
#include "hardware.h"
#include "image_loader.h"
#include "low_battery.h"
#include "menu_edge_swipe.h"
#include "ok_button_action.h"
#include "peripheral_power.h"
#include "power_latch.h"
#include "repo_qr.h"
#include "sd_card.h"
#include "sd_card_identity.h"
#include "sd_ota.h"
#include "sd_readonly_browser.h"
#include "text_render.h"
#include "local_time.h"
#include "ntp_sync.h"
#include "rtc_sync.h"
#include "usb_screen_capture.h"

// Runners-Journal includes
#include "dashboard_data.h"
#include "dashboard_fetch.h"
#include "dashboard_parse.h"
#include "runners_journal_config.h"
#include "secrets.h"
#include "../../common/include/wifi_sta.h"

#if RETERMINAL_MODEL != 1005
#error "Runners Journal supports only reTerminal E1005"
#endif

TimestampedLogger appLog(Serial1);
EPaper epaper;
usb_screen_capture::Server usbScreenCapture;

namespace {

using game_localization::Language;
using game_localization::TextId;

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 800;
constexpr char kAppName[] = "Runners Journal";
constexpr char kBrandName[] = "Runners Journal";
constexpr int kGridLeft = 40;
constexpr int kGridTop = 150;
constexpr int kCellSize = 80;
constexpr int kKeyboardKeyHeight = 42;
constexpr int kKeyboardFirstRowY = 586;
constexpr int kKeyboardRowGap = 6;
constexpr int kStatusDividerY = 48;
constexpr int kSwipeThreshold = 45;
constexpr int kMenuSwipeEdgeWidth = 40;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kBatteryCheckIntervalMs = 60000;
constexpr uint32_t kInactivitySleepMs = 5UL * 60UL * 1000UL;
constexpr int kLowBatteryThresholdPct = 5;
constexpr uint32_t kPersistedStateMagic = 0x47414D45;
constexpr uint16_t kPersistedStateVersion = 19;
constexpr char kReaderCjkFont16Path[] = "/fonts/epub_cjk_16.vlw";
constexpr char kReaderCjkFont16Name[] = "fonts/epub_cjk_16";
constexpr char kReaderCjkFont24Path[] = "/fonts/epub_cjk_24.vlw";
constexpr char kReaderCjkFont24Name[] = "fonts/epub_cjk_24";
constexpr char kReaderLatinFont16Path[] = "/fonts/sans_bold_16.vlw";
constexpr char kReaderLatinFont16Name[] = "fonts/sans_bold_16";
constexpr char kReaderLatinFont24Path[] = "/fonts/sans_bold_24.vlw";
constexpr char kReaderLatinFont24Name[] = "fonts/sans_bold_24";
constexpr size_t kReaderPathCapacity = 192;
constexpr size_t kReaderTextWidth = kScreenWidth - 36;
constexpr size_t kReaderLinesPerPage = 20;
constexpr int kReaderLineHeight = 32;
constexpr int kBrowserRowsPerPage = 8;
constexpr int kBrowserRowTop = 96;
constexpr int kBrowserRowHeight = 82;
constexpr int kReaderCoverLeft = 18;
constexpr int kReaderCoverTop = 64;
constexpr int kReaderCoverMaximumWidth = kScreenWidth - kReaderCoverLeft * 2;
constexpr int kReaderCoverMaximumHeight = 680;
constexpr int kMenuPreviewSize = 190;



struct Rect {
  int x;
  int y;
  int width;
  int height;

  bool contains(int pointX, int pointY) const {
    return pointX >= x && pointX < x + width &&
           pointY >= y && pointY < y + height;
  }
};

constexpr Rect kMenuCardSlots[] = {
    {40, 62, 190, 218},  {250, 62, 190, 218},
    {40, 292, 190, 218}, {250, 292, 190, 218},
    {40, 522, 190, 218}, {250, 522, 190, 218},
};
constexpr Rect kLanguageButtons[] = {
    {60, 145, 360, 72},
    {60, 245, 360, 72},
    {60, 345, 360, 72},
    {60, 445, 360, 72},
    {60, 545, 360, 72},
};
Rect kEpubReaderMenuCard = kMenuCardSlots[2];
Rect kRunnersJournalMenuCard = kMenuCardSlots[3];
constexpr Rect kPreviousPageButton = {8, 756, 48, 36};
constexpr Rect kNextPageButton = {424, 756, 48, 36};
constexpr Rect kBackButton = {8, 6, 48, 36};
constexpr Rect kSettingsButton = {8, 6, 36, 36};
constexpr Rect kHelpButton = {344, 6, 36, 36};
constexpr Rect kNewButton = {30, 688, 190, 66};
constexpr Rect kResetButton = {260, 688, 190, 66};
constexpr Rect kCenteredNewButton = {145, 688, 190, 66};
constexpr E1005FastRefresh::Region kBatteryStatusRegion = {390, 0, 90, 48};
constexpr E1005FastRefresh::Region kBoardRegion = {30, 80, 420, 600};
constexpr E1005FastRefresh::Region kPipeBoardRegion = {25, 80, 430, 585};
constexpr E1005FastRefresh::Region kFallingBlocksBoardRegion = {8, 64, 464,
                                                               620};
constexpr E1005FastRefresh::Region kReaderRegion = {0, 48, 480, 752};

enum class Screen {
  Menu,
  RunnersJournal,
  EpubBrowser,
  EpubReading,
};

enum class MenuPage : uint8_t {
  First,
  Second,
  Third,
};

enum class GameId : uint8_t {
  EpubReader,
  RunnersJournal,
  Count,
};

constexpr size_t kGameCount = static_cast<size_t>(GameId::Count);
constexpr uint8_t kGameOrder[kGameCount] = {
    static_cast<uint8_t>(GameId::RunnersJournal),
    static_cast<uint8_t>(GameId::EpubReader),
};
static_assert(sizeof(kGameOrder) / sizeof(kGameOrder[0]) == kGameCount);
constexpr size_t kGamesPerMenuPage =
    sizeof(kMenuCardSlots) / sizeof(kMenuCardSlots[0]);
constexpr size_t kMenuPageCount =
    (kGameCount + kGamesPerMenuPage - 1) / kGamesPerMenuPage;
static_assert(kMenuPageCount == 1);
constexpr bool validGameOrder() {
  bool seen[kGameCount] = {};
  for (const uint8_t game : kGameOrder) {
    if (game >= kGameCount || seen[game]) return false;
    seen[game] = true;
  }
  return true;
}
constexpr bool gameIsOnFirstMenuPage(GameId game) {
  for (size_t position = 0; position < kGamesPerMenuPage; ++position) {
    if (kGameOrder[position] == static_cast<uint8_t>(game)) return true;
  }
  return false;
}
static_assert(validGameOrder());
static_assert(gameIsOnFirstMenuPage(GameId::EpubReader));
const char* screenName(Screen screen) {
  switch (screen) {
    case Screen::Menu:
      return "menu";
    case Screen::EpubBrowser:
      return "EPUB browser";
    case Screen::EpubReading:
      return "saved EPUB page";
  }
  return "unknown";
}


struct ReaderResume {
  char browserPath[kReaderPathCapacity];
  char bookPath[kReaderPathCapacity];
  uint16_t chapter;
  uint32_t pageStart;
  uint8_t coverVisible;
  uint32_t cardSectorCount;
  uint32_t cardFingerprint;
};

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint8_t screen;
  uint8_t menuPage;
  uint32_t flags;
  ReaderResume reader;
  // Retained so version 18 RTC state keeps its existing layout.
  uint32_t legacyPlayCounts[kGameCount];
  uint32_t checksum;
};

RTC_DATA_ATTR PersistedState persistedState = {};

struct ButtonState {
  int pin;
  const char* name;
  int stableLevel;
  int sampledLevel;
  uint32_t changedAtMs;
  uint32_t pressedAtMs;
};

ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "OK / power", HIGH, HIGH, 0, 0},
    {board::PIN_BUTTON_1, "UP", HIGH, HIGH, 0, 0},
    {board::PIN_BUTTON_2, "DOWN", HIGH, HIGH, 0, 0},
};

struct ButtonEvent {
  ButtonState* button;
  uint32_t heldMs;
};

enum class SleepScreen {
  Resume,
  Charge,
};

// Forward declarations for runners-journal functions
void fetchRunnersJournalData();
void handleRunnersJournalInput();
void handleRunnersJournalButton(const ButtonEvent& event);
void showMenuPage(MenuPage page, bool beep);
void powerDownAndSleep(SleepScreen screen, int batteryPercent);
void pollTouch();

TwoWire touchWire(1);
Gt911Touch touch;
E1005FastRefresh fastRefresh(epaper);
EpubArchive epubArchive;
SdReadonlyBrowser sdBrowser;
Screen currentScreen = Screen::Menu;
MenuPage currentMenuPage = MenuPage::First;

// Runners-Journal Screen enum (must be defined before use)
enum class RunnersJournalScreen { Uke, Aar, Siste, Journal, Kart };
constexpr int kRunnersJournalScreenCount = 5;

// Runners-Journal state
namespace runners_journal {
  RunnersJournalScreen currentDashboardScreen = RunnersJournalScreen::Uke;
  dashboard::DashboardData dashboardData;
  bool dataFetched = false;
  uint32_t lastActivityTime = 0;
  bool timerWakesSuppressed = false;
  // True when the last fetch failed and the status screen is showing; lets
  // the OK handler offer the Wi-Fi portal instead of just refetching.
  bool showingFetchStatus = false;
}

bool touchReady = false;
bool touchActive = false;
bool touchActionHandled = false;
int16_t touchStartX = 0, touchStartY = 0;
int16_t touchEndX = 0, touchEndY = 0;

// OK button long-press tracking
uint32_t okButtonPressedAtMs = 0;
constexpr uint32_t kLongPressThresholdMs = 600;  // ~600ms for long-press

// Runners-Journal globals
// TODO: Integrate smoothFont from runners-journal for proper rendering
bool lightSleepReady = false;
Language currentLanguage = Language::English;
bool languageSelected = false;
bool languageSelectionVisible = false;
bool helpPaneVisible = false;
bool sdCardReady = false;
bool readerCjkFont16Available = false;
bool readerCjkFont24Available = false;
bool readerLatinFont16Available = false;
bool readerLatinFont24Available = false;
int browserPageStart = 0;
String browserMessage;
String readerBrowserPath = "/";
String readerBookPath;
String readerFolderCoverPath;
EpubChapterText readerChapterText;
bool readerChapterRequiresNonAscii = false;
bool readerChapterRequiresCjk = false;
int readerChapterIndex = 0;
size_t readerPageStart = 0;
bool readerCoverVisible = false;
sd_card_identity::Identity readerCardIdentity;
bool batteryStatusSampled = false;
bool externalPowerPresent = false;
int batteryPercent = -1;
uint32_t nextBatteryCheckAtMs = 0;
uint32_t lastActivityAtMs = 0;
uint32_t touchStartedAtMs = 0;
Gt911Touch::Point touchStart = {};
Gt911Touch::Point touchLast = {};

void configureButtons() {
  for (ButtonState& button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.stableLevel = digitalRead(button.pin);
    button.sampledLevel = button.stableLevel;
    button.changedAtMs = millis();
    button.pressedAtMs =
        button.stableLevel == LOW ? button.changedAtMs : 0;
  }
}

bool pollButtonEvent(ButtonEvent& event) {
  const uint32_t now = millis();
  for (ButtonState& button : buttons) {
    const int level = digitalRead(button.pin);
    if (level != button.sampledLevel) {
      button.sampledLevel = level;
      button.changedAtMs = now;
    }
    if (level == button.stableLevel ||
        now - button.changedAtMs < kButtonDebounceMs) {
      continue;
    }
    button.stableLevel = level;
    if (level == LOW) {
      button.pressedAtMs = now;
    } else {
      event = {&button, now - button.pressedAtMs};
      return true;
    }
  }
  return false;
}

bool inputHandlingActive() {
  if (touchActive) return true;
  for (const ButtonState& button : buttons) {
    if (button.sampledLevel != button.stableLevel ||
        button.stableLevel == LOW) {
      return true;
    }
  }
  return false;
}

void recordActivity() { lastActivityAtMs = millis(); }

const char* tr(TextId id) {
  return game_localization::text(currentLanguage, id);
}

const char* gameName(GameId game) {
  switch (game) {
    case GameId::EpubReader:
      return "EPUB Reader";
    case GameId::RunnersJournal:
      return "Løp";
    case GameId::Count:
      break;
  }
  return "unknown";
}

const char* shortEnglishGameName(GameId game) {
  switch (game) {
    case GameId::RunnersJournal:
      return "Løp";
    default:
      return gameName(game);
  }
}

TextId gameTextId(GameId game) {
  switch (game) {
    case GameId::EpubReader:
      return TextId::EpubReader;
    case GameId::RunnersJournal:
      return TextId::RunnersJournal;
    case GameId::Count:
      break;
  }
  return TextId::EpubReader;
}

GameId gameForScreen(Screen screen) {
  switch (screen) {
    case Screen::EpubBrowser:
    case Screen::EpubReading:
      return GameId::EpubReader;
    case Screen::Menu:
      return GameId::Count;
  }
  return GameId::Count;
}

GameId orderedGameAt(size_t position) {
  return static_cast<GameId>(kGameOrder[position]);
}

MenuPage menuPageForGame(GameId game) {
  for (size_t position = 0; position < kGameCount; ++position) {
    if (orderedGameAt(position) == game) {
      return static_cast<MenuPage>(position / kGamesPerMenuPage);
    }
  }
  return MenuPage::First;
}

uint32_t stateChecksum(const PersistedState& state) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
  uint32_t checksum = 2166136261UL;
  for (size_t index = 0; index < offsetof(PersistedState, checksum); ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619UL;
  }
  return checksum;
}

bool copyReaderPath(char* destination, size_t capacity, const String& path) {
  if (path.length() >= capacity) {
    destination[0] = '\0';
    return false;
  }
  memcpy(destination, path.c_str(), path.length() + 1);
  return true;
}

void saveResumeState() {
  PersistedState state = {};
  state.magic = kPersistedStateMagic;
  state.version = kPersistedStateVersion;
  state.screen = static_cast<uint8_t>(currentScreen);
  state.menuPage = static_cast<uint8_t>(currentMenuPage);
  const bool savedBrowserPath =
      copyReaderPath(state.reader.browserPath, sizeof(state.reader.browserPath),
                     readerBrowserPath);
  const bool savedBookPath =
      copyReaderPath(state.reader.bookPath, sizeof(state.reader.bookPath),
                     readerBookPath);
  state.reader.chapter = static_cast<uint16_t>(readerChapterIndex);
  state.reader.pageStart = static_cast<uint32_t>(readerPageStart);
  state.reader.coverVisible = readerCoverVisible ? 1 : 0;
  if (readerCardIdentity.valid()) {
    state.reader.cardSectorCount = readerCardIdentity.sectorCount;
    state.reader.cardFingerprint = readerCardIdentity.fingerprint;
  }
  if ((currentScreen == Screen::EpubBrowser && !savedBrowserPath) ||
      (currentScreen == Screen::EpubReading && !savedBookPath)) {
    state.screen = static_cast<uint8_t>(Screen::EpubBrowser);
    state.reader.browserPath[0] = '/';
    state.reader.browserPath[1] = '\0';
  }
  state.checksum = stateChecksum(state);
  persistedState = state;
}

bool restoreResumeState() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return false;

  const PersistedState state = persistedState;
  if (state.magic != kPersistedStateMagic ||
      state.version != kPersistedStateVersion ||
      state.checksum != stateChecksum(state) ||
      state.screen > static_cast<uint8_t>(Screen::EpubReading) ||
            state.flags != 0 ||
      memchr(state.reader.browserPath, '\0',
             sizeof(state.reader.browserPath)) == nullptr ||
      memchr(state.reader.bookPath, '\0', sizeof(state.reader.bookPath)) ==
          nullptr ||
      state.reader.chapter >= EpubArchive::kMaximumSpineItems ||
      state.reader.pageStart > EpubArchive::kMaximumChapterBytes) {
    LOG.println("[games] saved resume state is invalid");
    return false;
  }

  const Screen savedScreen = static_cast<Screen>(state.screen);
  if ((savedScreen == Screen::EpubBrowser ||
       savedScreen == Screen::EpubReading) &&
      state.reader.browserPath[0] != '/') {
    LOG.println("[games] saved EPUB browser path is invalid");
    return false;
  }
  if (savedScreen == Screen::EpubReading &&
      state.reader.bookPath[0] != '/') {
    LOG.println("[games] saved EPUB book path is invalid");
    return false;
  }
  if (state.reader.coverVisible > 1) {
    LOG.println("[games] saved EPUB cover state is invalid");
    return false;
  }
  readerBrowserPath =
      state.reader.browserPath[0] == '\0' ? "/" : state.reader.browserPath;
  readerBookPath = state.reader.bookPath;
  readerChapterIndex = state.reader.chapter;
  readerPageStart = state.reader.pageStart;
  readerCoverVisible = state.reader.coverVisible != 0;
  readerCardIdentity = {
      state.reader.cardSectorCount,
      state.reader.cardFingerprint,
  };
  currentMenuPage = static_cast<MenuPage>(state.menuPage);
  currentScreen = savedScreen;
  return true;
}

void disableLightSleepWake() {
  for (const ButtonState& button : buttons) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(button.pin));
  }
  if (board::PIN_TOUCH_INTERRUPT >= 0) {
    gpio_wakeup_disable(
        static_cast<gpio_num_t>(board::PIN_TOUCH_INTERRUPT));
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#if USB_SCREEN_CAPTURE_ENABLED
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);
#endif
  lightSleepReady = false;
}

bool configureLightSleepWake() {
  bool configured = true;
  for (const ButtonState& button : buttons) {
    configured =
        gpio_wakeup_enable(static_cast<gpio_num_t>(button.pin),
                           GPIO_INTR_LOW_LEVEL) == ESP_OK &&
        configured;
  }
  if (touchReady) {
    configured =
        gpio_wakeup_enable(
            static_cast<gpio_num_t>(board::PIN_TOUCH_INTERRUPT),
            GPIO_INTR_LOW_LEVEL) == ESP_OK &&
        configured;
  }
#if USB_SCREEN_CAPTURE_ENABLED
  const esp_err_t thresholdResult = uart_set_wakeup_threshold(UART_NUM_1, 3);
  const esp_err_t captureWakeResult =
      thresholdResult == ESP_OK ? esp_sleep_enable_uart_wakeup(UART_NUM_1)
                                : thresholdResult;
  if (captureWakeResult != ESP_OK) {
    LOG.printf("[games] USB serial wake unavailable: %s\n",
               esp_err_to_name(captureWakeResult));
  }
#endif
  configured = esp_sleep_enable_gpio_wakeup() == ESP_OK && configured;
  if (!configured) {
    disableLightSleepWake();
    LOG.println("[games] light-sleep GPIO wake unavailable");
  }
  return configured;
}

// True until a successful light sleep resets it; keeps the serial log
// readable when ESP_ERR_SLEEP_REJECT fires in fast bursts.
bool g_lightSleepDeferredLogged = false;

void idleInLightSleep() {
  if (!lightSleepReady) {
    delay(5);
    return;
  }
  if (inputHandlingActive()) {
    delay(5);
    return;
  }
  
  // Suppress timer-wakes in reader modes
  if (runners_journal::timerWakesSuppressed) {
    delay(5);
    return;
  }

  const uint32_t now = millis();
  const int32_t batteryRemainingMs =
      static_cast<int32_t>(nextBatteryCheckAtMs - now);
  const int32_t inactivityRemainingMs = static_cast<int32_t>(
      kInactivitySleepMs - static_cast<uint32_t>(now - lastActivityAtMs));
  if (batteryRemainingMs <= 0 || inactivityRemainingMs <= 0) return;
  const uint32_t sleepMs = std::min(
      static_cast<uint32_t>(batteryRemainingMs),
      static_cast<uint32_t>(inactivityRemainingMs));
  const uint64_t timerUs = static_cast<uint64_t>(sleepMs) * 1000ULL;
  if (esp_sleep_enable_timer_wakeup(timerUs) != ESP_OK) {
    LOG.println("[games] light-sleep timer wake unavailable");
    disableLightSleepWake();
    return;
  }

  LOG.println("[games] entering light sleep");
  LOG.flush();
  const esp_err_t sleepResult = esp_light_sleep_start();
  if (sleepResult == ESP_OK) {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char* wakeName = "other";
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
      wakeName = "gpio";
    } else if (cause == ESP_SLEEP_WAKEUP_UART) {
      wakeName = "uart";
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
      wakeName = "timer";
    }
    // A successful sleep ends any deferral burst; allow one notice next time.
    g_lightSleepDeferredLogged = false;
    LOG.printf("[games] exited light sleep (wake=%s)\n", wakeName);
#if USB_SCREEN_CAPTURE_ENABLED
    if (cause == ESP_SLEEP_WAKEUP_UART) {
      // The bytes which trigger ESP32-S3 UART wake are not retained. Stay
      // awake briefly so the host's repeated full command can be received.
      usbScreenCapture.serveFor(epaper, kScreenWidth, kScreenHeight, 300);
    }
#endif
  } else if (sleepResult == ESP_ERR_SLEEP_REJECT ||
             sleepResult == ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION) {
    // The flash/PSRAM power domains reject sleep while a transaction is
    // still settling, so this fires in bursts. Log the first deferral and
    // stay quiet until a successful sleep resets the flag.
    if (!g_lightSleepDeferredLogged) {
      g_lightSleepDeferredLogged = true;
      LOG.printf("[games] light sleep deferred: %s\n",
                 esp_err_to_name(sleepResult));
    }
    delay(10);
  } else {
    LOG.printf("[games] light sleep failed: %s\n",
               esp_err_to_name(sleepResult));
    disableLightSleepWake();
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

bool containsNonAscii(const String& text) {
  for (size_t index = 0; index < text.length(); ++index) {
    if (static_cast<uint8_t>(text[index]) >= 0x80) return true;
  }
  return false;
}

bool containsNonAscii(const char* text, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (static_cast<uint8_t>(text[index]) >= 0x80) return true;
  }
  return false;
}

const uint8_t* smoothFontFor(int pixelSize) {
  if (pixelSize >= 24) return game_ui_fonts::kGameUiFont24;
  return game_ui_fonts::kGameUiFont16;
}

void drawCenteredText(const String& text, int x, int y, int font,
                      uint16_t foreground, uint16_t background, int maxWidth,
                      bool forceSmoothFont = false) {
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(foreground, background, true);
  if (!forceSmoothFont && !containsNonAscii(text)) {
    int selectedFont = font == 6 ? 4 : font;
    if (selectedFont == 4 && epaper.textWidth(text, selectedFont) > maxWidth) {
      selectedFont = 2;
    }
    epaper.drawString(text, x, y, selectedFont);
    return;
  }

  int pixelSize = font >= 4 ? 24 : 16;
  epaper.loadFont(smoothFontFor(pixelSize));
  if (pixelSize > 16 && epaper.textWidth(text) > maxWidth) {
    epaper.unloadFont();
    pixelSize = pixelSize > 24 ? 24 : 16;
    epaper.loadFont(smoothFontFor(pixelSize));
    if (pixelSize > 16 && epaper.textWidth(text) > maxWidth) {
      epaper.unloadFont();
      epaper.loadFont(smoothFontFor(16));
    }
  }
  epaper.drawString(text, x, y);
  epaper.unloadFont();
}

void drawCentered(const String& text, int x, int y, int font) {
  drawCenteredText(text, x, y, font, TFT_BLACK, TFT_WHITE,
                   kScreenWidth - 16);
}

void drawStickyArcadeBrand(int centerY, int font) {
  drawCenteredText(kBrandName, kScreenWidth / 2, centerY, font, TFT_BLACK,
                   TFT_WHITE, kScreenWidth - 32, true);
}

void drawCenteredNumber(uint32_t value, int x, int y, int font,
                        uint16_t foreground, uint16_t background) {
  const int opticalYOffset = font == 6 ? 6 : font == 4 ? 3 : 0;
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(foreground, background, true);
  epaper.drawNumber(static_cast<long>(value), x, y + opticalYOffset, font);
}

void drawButton(const Rect& rect, const char* label) {
  const bool smoothFont = languageSelectionVisible ||
                          currentLanguage != Language::English;
  epaper.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 8, TFT_BLACK);
  drawCenteredText(label, rect.x + rect.width / 2,
                   rect.y + rect.height / 2 + (smoothFont ? -2 : 3), 4,
                   TFT_WHITE, TFT_BLACK, rect.width - 12, smoothFont);
}

void drawMenuCardFrame(const Rect& card) {
  epaper.fillRoundRect(card.x, card.y, card.width, kMenuPreviewSize, 14,
                      TFT_WHITE);
  epaper.drawRoundRect(card.x, card.y, card.width, kMenuPreviewSize, 14,
                      TFT_BLACK);
}

Rect& menuCardFor(GameId game) {
  switch (game) {
    case GameId::EpubReader:
      return kEpubReaderMenuCard;
    case GameId::RunnersJournal:
      return kRunnersJournalMenuCard;
    case GameId::Count:
      break;
  }
  return kEpubReaderMenuCard;
}

void drawEpubReaderMenuCard() {
  drawMenuCardFrame(kEpubReaderMenuCard);
  const int left = kEpubReaderMenuCard.x + 42;
  const int top = kEpubReaderMenuCard.y + 24;
  constexpr int kBookWidth = 106;
  constexpr int kBookHeight = 142;
  epaper.fillRoundRect(left, top, kBookWidth, kBookHeight, 5, TFT_BLACK);
  epaper.fillRect(left + 8, top + 7, kBookWidth - 16, kBookHeight - 14,
                  TFT_WHITE);
  epaper.fillRect(left + 16, top + 26, kBookWidth - 32, 4, TFT_BLACK);
  epaper.fillRect(left + 16, top + 42, kBookWidth - 32, 4, TFT_BLACK);
  epaper.fillRect(left + 16, top + 58, kBookWidth - 45, 4, TFT_BLACK);
  drawCentered("EPUB", left + kBookWidth / 2, top + 103, 2);
}

void drawRunnersJournalMenuCard() {
  drawMenuCardFrame(kRunnersJournalMenuCard);
  const int centerX = kRunnersJournalMenuCard.x + kRunnersJournalMenuCard.width / 2;
  const int centerY = kRunnersJournalMenuCard.y + kMenuPreviewSize / 2;
  // Single clean runner: head, torso lean, legs and arms as a few bold strokes.
  epaper.fillCircle(centerX - 12, centerY - 56, 11, TFT_BLACK);
  epaper.drawLine(centerX - 14, centerY - 42, centerX + 4, centerY - 12,
                  TFT_BLACK);
  epaper.drawWideLine(centerX - 14, centerY - 42, centerX + 4, centerY - 12,
                      5, TFT_BLACK);
  epaper.drawWideLine(centerX + 4, centerY - 12, centerX - 26, centerY + 6,
                      4, TFT_BLACK);
  epaper.drawWideLine(centerX + 4, centerY - 12, centerX + 34, centerY + 26,
                      4, TFT_BLACK);
  epaper.drawWideLine(centerX - 10, centerY - 34, centerX - 40, centerY - 18,
                      4, TFT_BLACK);
  epaper.drawWideLine(centerX - 10, centerY - 34, centerX + 20, centerY - 26,
                      4, TFT_BLACK);
  epaper.drawFastHLine(centerX - 56, centerY + 34, 112, TFT_BLACK);
  drawCentered("KM", centerX + 44, centerY - 76, 2);
}

void arrangeMenuCards() {
  for (size_t position = 0; position < kGameCount; ++position) {
    menuCardFor(orderedGameAt(position)) =
        kMenuCardSlots[position % kGamesPerMenuPage];
  }
}

void drawGameMenuCard(GameId game) {
  switch (game) {
    case GameId::EpubReader:
      drawEpubReaderMenuCard();
      break;
    case GameId::RunnersJournal:
      drawRunnersJournalMenuCard();
      break;
    case GameId::Count:
      return;
  }

  const Rect& card = menuCardFor(game);
  String label = currentLanguage == Language::English
                     ? (game == GameId::EpubReader ? "Book Reader"
                                                   : gameName(game))
                     : tr(gameTextId(game));
  if (currentLanguage == Language::English &&
      epaper.textWidth(label, 4) > card.width - 16) {
    label = shortEnglishGameName(game);
  }
  drawCenteredText(label, card.x + card.width / 2,
                   card.y + kMenuPreviewSize + 18, 4, TFT_BLACK, TFT_WHITE,
                   card.width - 16, currentLanguage != Language::English);
}

void drawBatteryStatus() {
  constexpr int kCenterY = 24;
  constexpr int kEdgeInset = 6;
  constexpr int kGaugeWidth = 22;
  constexpr int kGaugeHeight = 12;
  constexpr int kTerminalWidth = 5;
  constexpr int kTerminalHeight = 5;
  constexpr int kOutline = 1;
  const int gaugeX =
      kScreenWidth - kEdgeInset - kTerminalWidth - kGaugeWidth;
  const int gaugeY = kCenterY + 2 - kGaugeHeight / 2;
  const String percent =
      batteryPercent >= 0 ? String(batteryPercent) + "%" : "--%";

  epaper.setFreeFont(&FreeSansBold9pt7b);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(percent, gaugeX - 9, kCenterY, 1);
  text_render::drawBatteryGauge(
      epaper, gaugeX, gaugeY, kGaugeWidth, kGaugeHeight, batteryPercent,
      kOutline, kTerminalWidth, kTerminalHeight, TFT_BLACK, TFT_WHITE,
      externalPowerPresent);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

void drawStatusBar() {
  drawBatteryStatus();
  epaper.drawFastHLine(0, kStatusDividerY, kScreenWidth, TFT_BLACK);
}

void drawArrowButton(const Rect& button, bool pointsRight) {
  epaper.fillRoundRect(button.x, button.y, button.width, button.height, 8,
                      TFT_BLACK);
  const int centerY = button.y + button.height / 2;
  const int tipX =
      pointsRight ? button.x + button.width - 9 : button.x + 9;
  const int headBaseX = pointsRight ? tipX - 13 : tipX + 13;
  const int tailX =
      pointsRight ? button.x + 9 : button.x + button.width - 9;
  epaper.fillTriangle(tipX, centerY, headBaseX, centerY - 11, headBaseX,
                     centerY + 11, TFT_WHITE);
  if (pointsRight) {
    epaper.fillRect(tailX, centerY - 3, headBaseX - tailX + 1, 7, TFT_WHITE);
  } else {
    epaper.fillRect(headBaseX - 1, centerY - 3, tailX - headBaseX + 1, 7,
                   TFT_WHITE);
  }
}

void drawBackIndicator() {
  drawArrowButton(kBackButton, false);
}

void drawHelpIndicator() {
  const int centerX = kHelpButton.x + kHelpButton.width / 2;
  const int centerY = kHelpButton.y + kHelpButton.height / 2;
  epaper.fillRect(kHelpButton.x - 2, kHelpButton.y - 2,
                  kHelpButton.width + 4, kHelpButton.height + 4, TFT_WHITE);
  if (helpPaneVisible) {
    epaper.fillCircle(centerX, centerY, 17, TFT_BLACK);
    for (int offset = -1; offset <= 1; ++offset) {
      epaper.drawLine(centerX - 7, centerY - 7 + offset, centerX + 7,
                      centerY + 7 + offset, TFT_WHITE);
      epaper.drawLine(centerX - 7, centerY + 7 + offset, centerX + 7,
                      centerY - 7 + offset, TFT_WHITE);
    }
  } else {
    epaper.drawCircle(centerX, centerY, 17, TFT_BLACK);
    epaper.drawCircle(centerX, centerY, 16, TFT_BLACK);
    drawCenteredText("?", centerX, centerY + 2, 4, TFT_BLACK, TFT_WHITE, 24);
  }
}

void drawSettingsIndicator() {
  const int centerX = kSettingsButton.x + kSettingsButton.width / 2;
  const int centerY = kSettingsButton.y + kSettingsButton.height / 2;
  constexpr int kGlyphRadius = 14;
  constexpr int kToothLength = 6;
  constexpr int kToothThickness = 5;
  const int left = centerX - kGlyphRadius;
  const int top = centerY - kGlyphRadius;
  epaper.fillRect(kSettingsButton.x - 2, kSettingsButton.y - 2,
                  kSettingsButton.width + 4, kSettingsButton.height + 4,
                  TFT_WHITE);
  epaper.fillRect(centerX - kToothThickness / 2, top, kToothThickness,
                  kToothLength, TFT_BLACK);
  epaper.fillRect(centerX - kToothThickness / 2,
                  centerY + kGlyphRadius - kToothLength, kToothThickness,
                  kToothLength, TFT_BLACK);
  epaper.fillRect(left, centerY - kToothThickness / 2, kToothLength,
                  kToothThickness, TFT_BLACK);
  epaper.fillRect(centerX + kGlyphRadius - kToothLength,
                  centerY - kToothThickness / 2, kToothLength,
                  kToothThickness, TFT_BLACK);
  epaper.fillRect(centerX - 11, centerY - 11, 5, 5, TFT_BLACK);
  epaper.fillRect(centerX + 6, centerY - 11, 5, 5, TFT_BLACK);
  epaper.fillRect(centerX - 11, centerY + 6, 5, 5, TFT_BLACK);
  epaper.fillRect(centerX + 6, centerY + 6, 5, 5, TFT_BLACK);
  epaper.fillCircle(centerX, centerY, 9, TFT_BLACK);
  epaper.fillCircle(centerX, centerY, 4, TFT_WHITE);
}

void drawGameStatusBar(const char* title) {
  drawBackIndicator();
  drawCenteredText(title, kScreenWidth / 2, 24, 4, TFT_BLACK, TFT_WHITE, 250,
                   currentLanguage != Language::English);
  drawHelpIndicator();
  drawStatusBar();
}

game_help::Topic helpTopicForScreen() {
  switch (currentScreen) {
    case Screen::EpubBrowser:
    case Screen::EpubReading:
      return game_help::Topic::EpubReader;
    case Screen::Menu:
      break;
  }
  return game_help::Topic::LightsOut;
}

size_t helpCharacterWidth(uint32_t codepoint, epub_text::TextStyle) {
  char encoded[5] = {};
  if (codepoint <= 0x7F) {
    encoded[0] = static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    encoded[0] = static_cast<char>(0xC0 | (codepoint >> 6));
    encoded[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    encoded[0] = static_cast<char>(0xE0 | (codepoint >> 12));
    encoded[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    encoded[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    encoded[0] = static_cast<char>(0xF0 | (codepoint >> 18));
    encoded[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    encoded[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    encoded[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
  }
  return static_cast<size_t>(epaper.textWidth(encoded));
}

void drawJustifiedHelpLine(const std::string& line, int x, int y,
                           int maximumWidth, bool justify) {
  if (!justify || line.empty()) {
    epaper.drawString(line.c_str(), x, y);
    return;
  }

  const size_t spaceCount =
      static_cast<size_t>(std::count(line.begin(), line.end(), ' '));
  if (spaceCount > 0) {
    const int spaceWidth = epaper.textWidth(" ");
    int naturalWidth = static_cast<int>(spaceCount) * spaceWidth;
    size_t runStart = 0;
    for (size_t offset = 0; offset <= line.length(); ++offset) {
      if (offset < line.length() && line[offset] != ' ') continue;
      if (offset > runStart) {
        naturalWidth += epaper.textWidth(
            String(line.substr(runStart, offset - runStart).c_str()));
      }
      runStart = offset + 1;
    }

    const int extraWidth = std::max(0, maximumWidth - naturalWidth);
    const int extraPerSpace = extraWidth / static_cast<int>(spaceCount);
    const int extraRemainder = extraWidth % static_cast<int>(spaceCount);
    size_t spaceIndex = 0;
    runStart = 0;
    for (size_t offset = 0; offset <= line.length(); ++offset) {
      if (offset < line.length() && line[offset] != ' ') continue;
      if (offset > runStart) {
        const String run(line.substr(runStart, offset - runStart).c_str());
        epaper.drawString(run, x, y);
        x += epaper.textWidth(run);
      }
      if (offset < line.length()) {
        x += spaceWidth + extraPerSpace +
             (spaceIndex < static_cast<size_t>(extraRemainder) ? 1 : 0);
        ++spaceIndex;
      }
      runStart = offset + 1;
    }
    return;
  }

  size_t glyphCount = 0;
  int naturalWidth = 0;
  for (size_t offset = 0; offset < line.length();) {
    const size_t bytes =
        epub_text::utf8CharacterBytes(line.data(), line.length(), offset);
    naturalWidth +=
        epaper.textWidth(String(line.substr(offset, bytes).c_str()));
    offset += bytes;
    ++glyphCount;
  }
  if (glyphCount < 2) {
    epaper.drawString(line.c_str(), x, y);
    return;
  }

  const size_t gapCount = glyphCount - 1;
  const int extraWidth = std::max(0, maximumWidth - naturalWidth);
  const int extraPerGap = extraWidth / static_cast<int>(gapCount);
  const int extraRemainder = extraWidth % static_cast<int>(gapCount);
  size_t glyphIndex = 0;
  for (size_t offset = 0; offset < line.length();) {
    const size_t bytes =
        epub_text::utf8CharacterBytes(line.data(), line.length(), offset);
    const String glyph(line.substr(offset, bytes).c_str());
    epaper.drawString(glyph, x, y);
    x += epaper.textWidth(glyph);
    if (glyphIndex < gapCount) {
      x += extraPerGap +
           (glyphIndex < static_cast<size_t>(extraRemainder) ? 1 : 0);
    }
    offset += bytes;
    ++glyphIndex;
  }
}

void drawHelpPane() {
  constexpr int kPaneLeft = 14;
  constexpr int kPaneTop = 64;
  constexpr int kPaneWidth = kScreenWidth - kPaneLeft * 2;
  constexpr int kPaneHeight = 672;
  constexpr int kTextLeft = 28;
  constexpr int kTextRight = kPaneLeft + kPaneWidth - 14;
  constexpr int kTextWidth = kTextRight - kTextLeft;
  constexpr int kTextTop = 126;
  constexpr int kLineHeight = 28;

  epaper.fillRect(0, kStatusDividerY + 1, kScreenWidth,
                  kScreenHeight - kStatusDividerY - 1, TFT_WHITE);
  drawHelpIndicator();
  epaper.drawRoundRect(kPaneLeft, kPaneTop, kPaneWidth, kPaneHeight, 12,
                      TFT_BLACK);
  epaper.drawRoundRect(kPaneLeft + 1, kPaneTop + 1, kPaneWidth - 2,
                      kPaneHeight - 2, 11, TFT_BLACK);
  drawCenteredText(tr(TextId::HowToPlay), kScreenWidth / 2, 92, 4, TFT_BLACK,
                   TFT_WHITE, kPaneWidth - 32,
                   currentLanguage != Language::English);

  const char* instructions =
      game_help::text(currentLanguage, helpTopicForScreen());
  const size_t instructionLength = strlen(instructions);
  epaper.loadFont(currentLanguage == Language::ChineseSimplified
                       ? game_ui_fonts::kGameHelpFont24
                       : epub_latin_fonts::kRegular);
  const epub_text::TextPage page = epub_text::paginate(
      instructions, instructionLength, 0, kTextWidth,
      game_help::kMaximumLines, epub_text::TextStyle::Regular, true,
      helpCharacterWidth);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(TL_DATUM);
  for (size_t line = 0; line < page.lines.size(); ++line) {
    const bool justify =
        (line < page.justifyLines.size() && page.justifyLines[line]) ||
        (currentLanguage == Language::ChineseSimplified &&
         line + 1 < page.lines.size());
    drawJustifiedHelpLine(
        page.lines[line], kTextLeft,
        kTextTop + static_cast<int>(line) * kLineHeight, kTextWidth, justify);
  }
  epaper.unloadFont();
  epaper.setTextFont(2);
}

void drawMenu() {
  epaper.fillSprite(TFT_WHITE);
  drawSettingsIndicator();
  drawStickyArcadeBrand(24, 4);
  arrangeMenuCards();
  const size_t pageIndex = static_cast<size_t>(currentMenuPage);
  const size_t firstPosition = pageIndex * kGamesPerMenuPage;
  const size_t visibleGames =
      std::min(kGamesPerMenuPage, kGameCount - firstPosition);
  for (size_t slot = 0; slot < visibleGames; ++slot) {
    drawGameMenuCard(orderedGameAt(firstPosition + slot));
  }
  if (pageIndex > 0) drawArrowButton(kPreviousPageButton, false);
  if (pageIndex + 1 < kMenuPageCount) {
    drawArrowButton(kNextPageButton, true);
  }
  const String pageLabel =
      String(pageIndex + 1) + " / " + String(kMenuPageCount);
  drawCentered(pageLabel, kScreenWidth / 2, 774, 4);
  drawStatusBar();
}

void drawLanguageSelection() {
  epaper.fillSprite(TFT_WHITE);
  drawStickyArcadeBrand(24, 4);
  drawCentered(tr(TextId::SelectLanguage), kScreenWidth / 2, 72, 4);
  for (size_t index = 0; index < game_localization::kLanguageCount; ++index) {
    drawButton(kLanguageButtons[index],
               game_localization::languageName(static_cast<Language>(index)));
  }
  drawStatusBar();
}

void drawStatus(const char* title, const char* detail) {
  epaper.fillSprite(TFT_WHITE);
  drawCentered(title, kScreenWidth / 2, 330, 4);
  drawCentered(detail, kScreenWidth / 2, 390, 4);
}

void drawRepoQr() {
  repo_qr::drawBottomRight(epaper, kScreenWidth, kScreenHeight,
                           /*moduleSize=*/2, /*margin=*/12, TFT_BLACK,
                           TFT_WHITE);
}

void drawSleepSplash() {
  epaper.fillSprite(TFT_WHITE);
  drawStickyArcadeBrand(390, 6);
  drawRepoQr();
}

void drawChargeSplash(int batteryPercent) {
  epaper.fillSprite(TFT_WHITE);
  drawCentered(tr(TextId::BatteryLow), kScreenWidth / 2, 130, 4);
  drawCentered(String(batteryPercent) + "% " + tr(TextId::Remaining),
               kScreenWidth / 2, 180, 4);
  drawStickyArcadeBrand(390, 6);
}

String fitReaderText(String text, int maximumWidth, int builtInFont = 0) {
  const auto textWidth = [builtInFont](const String& value) {
    return builtInFont > 0 ? epaper.textWidth(value, builtInFont)
                           : epaper.textWidth(value);
  };
  if (textWidth(text) <= maximumWidth) return text;
  constexpr char kEllipsis[] = "...";
  while (!text.isEmpty() && textWidth(text + kEllipsis) > maximumWidth) {
    size_t offset = text.length() - 1;
    while (offset > 0 &&
           (static_cast<uint8_t>(text[offset]) & 0xC0) == 0x80) {
      --offset;
    }
    text.remove(offset);
  }
  return text + kEllipsis;
}

void drawReaderString(const String& text, int x, int y, int builtInFont = 0) {
  if (builtInFont > 0) {
    epaper.drawString(text, x, y, builtInFont);
  } else {
    epaper.drawString(text, x, y);
  }
}

const uint8_t* embeddedReaderFont(epub_text::TextStyle style) {
  switch (style) {
    case epub_text::TextStyle::Bold:
      return epub_latin_fonts::kBold;
    case epub_text::TextStyle::Italic:
      return epub_latin_fonts::kItalic;
    case epub_text::TextStyle::BoldItalic:
      return epub_latin_fonts::kBoldItalic;
    case epub_text::TextStyle::Regular:
      return epub_latin_fonts::kRegular;
  }
  return epub_latin_fonts::kRegular;
}

size_t readerGlyphWidth(uint32_t codepoint, epub_text::TextStyle style,
                        bool embeddedLatin) {
  if (codepoint >= 1 && codepoint <= 4) return 0;
  if ((codepoint >= 0x0300 && codepoint <= 0x036F) ||
      (codepoint >= 0xFE00 && codepoint <= 0xFE0F)) {
    return 0;
  }
  if (embeddedLatin && epub_latin_fonts::supports(codepoint)) {
    return epub_latin_fonts::advance(static_cast<uint8_t>(style), codepoint);
  }

  uint8_t width = 0;
  const size_t fallbackWidth =
      epub_latin_fonts::maximumFallbackAdvance(codepoint, width) ? width : 24;
  return fallbackWidth +
         ((!embeddedLatin &&
           (style == epub_text::TextStyle::Bold ||
            style == epub_text::TextStyle::BoldItalic))
              ? 1
              : 0);
}

size_t readerLineWidth(const std::string& line,
                       epub_text::TextStyle initialStyle,
                       bool embeddedLatin) {
  size_t width = 0;
  epub_text::TextStyle style = initialStyle;
  for (size_t offset = 0; offset < line.length();) {
    if (epub_text::isStyleMarker(line[offset])) {
      style = epub_text::styleFromMarker(line[offset++]);
      continue;
    }
    const uint32_t codepoint =
        epub_text::utf8Codepoint(line.data(), line.length(), offset);
    width += readerGlyphWidth(codepoint, style, embeddedLatin);
    offset += epub_text::utf8CharacterBytes(line.data(), line.length(), offset);
  }
  return width;
}

void drawStyledReaderLine(const std::string& line, int x, int y,
                          epub_text::TextStyle& style, bool embeddedLatin,
                          epub_text::TextStyle& loadedStyle,
                          bool& embeddedFontLoaded, bool justify) {
  const size_t spaceCount =
      static_cast<size_t>(std::count(line.begin(), line.end(), ' '));
  const size_t lineWidth = readerLineWidth(line, style, embeddedLatin);
  const size_t extraWidth =
      justify && spaceCount > 0 && lineWidth < kReaderTextWidth
          ? kReaderTextWidth - lineWidth
          : 0;
  const size_t extraPerSpace =
      spaceCount > 0 ? extraWidth / spaceCount : 0;
  const size_t extraRemainder =
      spaceCount > 0 ? extraWidth % spaceCount : 0;
  size_t spaceIndex = 0;
  size_t runStart = 0;
  for (size_t offset = 0; offset <= line.length(); ++offset) {
    const bool atEnd = offset == line.length();
    const bool atStyle =
        !atEnd && epub_text::isStyleMarker(line[offset]);
    const bool atSpace = !atEnd && line[offset] == ' ';
    if (!atEnd && !atStyle && !atSpace) continue;

    if (offset > runStart) {
      const String run(line.substr(runStart, offset - runStart).c_str());
      if (embeddedLatin &&
          (!embeddedFontLoaded || loadedStyle != style)) {
        epaper.loadFont(embeddedReaderFont(style));
        loadedStyle = style;
        embeddedFontLoaded = true;
      }
      epaper.drawString(run, x, y);
      const int width = epaper.textWidth(run);
      if (!embeddedLatin &&
          (style == epub_text::TextStyle::Bold ||
           style == epub_text::TextStyle::BoldItalic)) {
        epaper.setTextColor(TFT_BLACK, TFT_BLACK, false);
        epaper.drawString(run, x + 1, y);
        epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
      }
      if (!embeddedLatin &&
          (style == epub_text::TextStyle::Italic ||
           style == epub_text::TextStyle::BoldItalic)) {
        epaper.drawFastHLine(x, y + 27, width, TFT_BLACK);
      }
      x += width;
    }
    if (atStyle) {
      style = epub_text::styleFromMarker(line[offset]);
    } else if (atSpace) {
      x += static_cast<int>(readerGlyphWidth(' ', style, embeddedLatin) +
                            extraPerSpace +
                            (spaceIndex < extraRemainder ? 1 : 0));
      ++spaceIndex;
    }
    runStart = offset + 1;
  }
}

bool usesEmbeddedReaderFont(const char* text, size_t length) {
  for (size_t offset = 0; offset < length;) {
    if (epub_text::isStyleMarker(text[offset])) {
      ++offset;
      continue;
    }
    const uint32_t codepoint =
        epub_text::utf8Codepoint(text, length, offset);
    if (!epub_latin_fonts::supports(codepoint)) return false;
    offset += epub_text::utf8CharacterBytes(text, length, offset);
  }
  return true;
}

bool pageUsesEmbeddedReaderFont(const epub_text::TextPage& page) {
  for (const std::string& line : page.lines) {
    if (!usesEmbeddedReaderFont(line.data(), line.length())) return false;
  }
  return true;
}

bool pageRequiresCjk(const epub_text::TextPage& page) {
  for (const std::string& line : page.lines) {
    if (epub_text::containsCjk(line.data(), line.length())) return true;
  }
  return false;
}

size_t readerCharacterWidth(uint32_t codepoint,
                            epub_text::TextStyle style) {
  return readerGlyphWidth(codepoint, style, true);
}

bool sdCardInserted() {
  pinMode(board::PIN_SD_DETECT, INPUT_PULLUP);
  delayMicroseconds(50);
  return digitalRead(board::PIN_SD_DETECT) == LOW;
}

bool readReaderCardIdentity(sd_card_identity::Identity& identity) {
  identity = {};
  if (!sdCardInserted()) return false;
  const size_t sectors = SD.numSectors();
  if (sectors == 0 || sectors > UINT32_MAX) return false;

  uint8_t sectorZero[512] = {};
  if (!SD.readRAW(sectorZero, 0)) return false;
  const uint32_t partitionStart =
      sd_card_identity::partitionStartSector(sectorZero, sizeof(sectorZero));
  uint8_t volumeBoot[512] = {};
  const uint8_t* volumeBootData = sectorZero;
  if (partitionStart > 0 && partitionStart < sectors) {
    if (!SD.readRAW(volumeBoot, partitionStart)) return false;
    volumeBootData = volumeBoot;
  }
  identity = sd_card_identity::identify(
      static_cast<uint32_t>(sectors), sectorZero, sizeof(sectorZero),
      volumeBootData);
  return identity.valid();
}

void detectReaderFonts() {
  if (!sdCardReady) {
    readerCjkFont16Available = false;
    readerCjkFont24Available = false;
    readerLatinFont16Available = false;
    readerLatinFont24Available = false;
    return;
  }
  readerCjkFont16Available = sd_card::fileExists(kReaderCjkFont16Path);
  readerCjkFont24Available = sd_card::fileExists(kReaderCjkFont24Path);
  readerLatinFont16Available = sd_card::fileExists(kReaderLatinFont16Path);
  readerLatinFont24Available = sd_card::fileExists(kReaderLatinFont24Path);
  LOG.printf(
      "[games] reader fonts: CJK 16=%s 24=%s, Latin 16=%s 24=%s\n",
      readerCjkFont16Available ? "yes" : "no",
      readerCjkFont24Available ? "yes" : "no",
      readerLatinFont16Available ? "yes" : "no",
      readerLatinFont24Available ? "yes" : "no");
}

enum class ReaderStorageStatus {
  Ready,
  Changed,
  Missing,
};

void clearReaderStorageState() {
  epubArchive.close();
  readerChapterText.clear();
  readerChapterRequiresNonAscii = false;
  readerChapterRequiresCjk = false;
  readerBookPath = "";
  readerFolderCoverPath = "";
  readerPageStart = 0;
  readerChapterIndex = 0;
  readerCoverVisible = false;
  readerBrowserPath = "/";
  browserPageStart = 0;
  browserMessage = "";
  readerCjkFont16Available = false;
  readerCjkFont24Available = false;
  readerLatinFont16Available = false;
  readerLatinFont24Available = false;
}

ReaderStorageStatus refreshReaderStorage() {
  if (!sdCardInserted()) {
    clearReaderStorageState();
    if (sdCardReady) SD.end();
    sdCardReady = false;
    return ReaderStorageStatus::Missing;
  }

  bool remounted = false;
  if (!sdCardReady) {
    epubArchive.close();
    sdCardReady =
        sd_card::mount(epaper.getSPIinstance(), "/sticky-arcade");
    if (!sdCardReady) {
      clearReaderStorageState();
      return ReaderStorageStatus::Missing;
    }
    remounted = true;
  }

  sd_card_identity::Identity currentIdentity;
  if (!readReaderCardIdentity(currentIdentity)) {
    clearReaderStorageState();
    SD.end();
    sdCardReady = false;
    return ReaderStorageStatus::Missing;
  }

  const bool changed =
      readerCardIdentity.valid() &&
      !sd_card_identity::same(readerCardIdentity, currentIdentity);
  if (changed) {
    LOG.println("[games] SD card changed; resetting EPUB reader state");
    clearReaderStorageState();
  }
  readerCardIdentity = currentIdentity;
  if (changed || remounted) detectReaderFonts();
  return changed ? ReaderStorageStatus::Changed
                 : ReaderStorageStatus::Ready;
}

bool loadReaderFont(int pixelSize, bool cjkRequired) {
  if (cjkRequired) {
    if (pixelSize >= 24 && readerCjkFont24Available) {
      epaper.loadFont(kReaderCjkFont24Name, SD);
      return true;
    }
    if (readerCjkFont16Available) {
      epaper.loadFont(kReaderCjkFont16Name, SD);
      return true;
    }
    epaper.loadFont(smoothFontFor(pixelSize));
    return false;
  }

  if (pixelSize >= 24 && readerLatinFont24Available) {
    epaper.loadFont(kReaderLatinFont24Name, SD);
  } else if (readerLatinFont16Available) {
    epaper.loadFont(kReaderLatinFont16Name, SD);
  } else {
    epaper.loadFont(smoothFontFor(pixelSize));
  }
  return true;
}

bool browserPageRequiresCjk() {
  if (epub_text::containsCjk(readerBrowserPath.c_str(),
                             readerBrowserPath.length())) {
    return true;
  }
  for (int row = 0; row < kBrowserRowsPerPage; ++row) {
    const int itemIndex = browserPageStart + row;
    if (epub_browser_logic::isParentFolderItem(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str()))) {
      const char* name = tr(TextId::UpFolder);
      if (epub_text::containsCjk(name, strlen(name))) return true;
      continue;
    }
    const SdReadonlyBrowser::Entry* entry =
        sdBrowser.entry(epub_browser_logic::storedEntryIndex(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str())));
    if (entry != nullptr &&
        epub_text::containsCjk(entry->name.c_str(), entry->name.length())) {
      return true;
    }
  }
  return false;
}

bool browserPageRequiresNonAscii() {
  if (containsNonAscii(readerBrowserPath)) return true;
  for (int row = 0; row < kBrowserRowsPerPage; ++row) {
    const int itemIndex = browserPageStart + row;
    if (epub_browser_logic::isParentFolderItem(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str()))) {
      const char* name = tr(TextId::UpFolder);
      if (containsNonAscii(name, strlen(name))) return true;
      continue;
    }
    const SdReadonlyBrowser::Entry* entry =
        sdBrowser.entry(epub_browser_logic::storedEntryIndex(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str())));
    if (entry != nullptr && containsNonAscii(entry->name)) return true;
  }
  return false;
}

int browserItemCount() {
  return epub_browser_logic::itemCount(
      sdBrowser.count(),
      epub_browser_logic::hasParentFolder(readerBrowserPath.c_str()));
}

bool hasPreviousBrowserPage() { return browserPageStart > 0; }

bool hasNextBrowserPage() {
  return browserPageStart + kBrowserRowsPerPage < browserItemCount();
}

void drawBrowserFolderIcon(int x, int y) {
  epaper.fillRect(x + 2, y + 8, 44, 31, TFT_BLACK);
  epaper.fillRect(x + 7, y + 2, 19, 10, TFT_BLACK);
  epaper.fillRect(x + 6, y + 13, 36, 22, TFT_WHITE);
}

void drawBrowserEntryIcon(const SdReadonlyBrowser::Entry& entry, int x,
                          int y) {
  if (entry.directory) {
    drawBrowserFolderIcon(x, y);
    return;
  }
  epaper.drawRect(x + 7, y, 34, 44, TFT_BLACK);
  epaper.fillTriangle(x + 30, y, x + 41, y, x + 41, y + 11, TFT_BLACK);
  if (entry.epub) {
    epaper.fillRect(x + 12, y + 18, 24, 4, TFT_BLACK);
    epaper.fillRect(x + 12, y + 28, 24, 4, TFT_BLACK);
  } else {
    epaper.drawLine(x + 13, y + 17, x + 35, y + 37, TFT_BLACK);
    epaper.drawLine(x + 35, y + 17, x + 13, y + 37, TFT_BLACK);
  }
}

void drawBrowserParentFolderIcon(int x, int y) {
  drawBrowserFolderIcon(x, y);
  epaper.fillTriangle(x + 24, y + 15, x + 15, y + 26, x + 33, y + 26,
                      TFT_BLACK);
  epaper.fillRect(x + 21, y + 25, 7, 9, TFT_BLACK);
}

void drawEpubBrowser() {
  epaper.fillSprite(TFT_WHITE);
  drawGameStatusBar(tr(TextId::EpubReader));
  if (!sdCardReady) {
    drawCentered(tr(TextId::SdCardRequired), kScreenWidth / 2, 320, 4);
    drawCentered(tr(TextId::InsertSdCard), kScreenWidth / 2, 380, 4);
    return;
  }

  const bool smoothBrowserFont = browserPageRequiresNonAscii();
  const bool browserFontReady =
      !smoothBrowserFont || loadReaderFont(24, browserPageRequiresCjk());
  const int builtInFont = smoothBrowserFont ? 0 : 4;
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  drawReaderString(
      fitReaderText(readerBrowserPath, kScreenWidth - 36, builtInFont),
      kScreenWidth / 2, 72, builtInFont);
  const bool hasParentFolder =
      epub_browser_logic::hasParentFolder(readerBrowserPath.c_str());
  const int itemCount = browserItemCount();
  if (itemCount == 0) {
    if (smoothBrowserFont) epaper.unloadFont();
    drawCentered(tr(TextId::EmptyFolder), kScreenWidth / 2, 350, 4);
  } else {
    browserPageStart =
        std::max(0, std::min(browserPageStart,
                             std::max(0, itemCount - 1)));
    epaper.setTextDatum(ML_DATUM);
    for (int row = 0; row < kBrowserRowsPerPage; ++row) {
      const int index = browserPageStart + row;
      if (index >= itemCount) break;
      const int top = kBrowserRowTop + row * kBrowserRowHeight;
      epaper.drawFastHLine(18, top + kBrowserRowHeight - 1,
                          kScreenWidth - 36, TFT_BLACK);
      String name;
      if (epub_browser_logic::isParentFolderItem(index, hasParentFolder)) {
        drawBrowserParentFolderIcon(22, top + 16);
        name = tr(TextId::UpFolder);
      } else {
        const SdReadonlyBrowser::Entry* entry =
            sdBrowser.entry(epub_browser_logic::storedEntryIndex(
                index, hasParentFolder));
        if (entry == nullptr) break;
        drawBrowserEntryIcon(*entry, 22, top + 16);
        name = entry->name;
      }
      name = fitReaderText(name, 365, builtInFont);
      drawReaderString(name, 82, top + kBrowserRowHeight / 2, builtInFont);
    }
    if (smoothBrowserFont) epaper.unloadFont();
  }

  String footer = browserMessage;
  if (footer.isEmpty() && !browserFontReady) {
    footer = tr(TextId::CjkFontRequired);
  }
  if (sdBrowser.truncated()) {
    if (!footer.isEmpty()) footer += " - ";
    footer += "96+";
  }
  if (!footer.isEmpty()) drawCentered(footer, kScreenWidth / 2, 776, 2);
  if (hasPreviousBrowserPage()) {
    drawArrowButton(kPreviousPageButton, false);
  }
  if (hasNextBrowserPage()) {
    drawArrowButton(kNextPageButton, true);
  }
}

epub_text::TextPage currentReaderPage() {
  return epub_text::paginate(
      readerChapterText.c_str(), readerChapterText.length(), readerPageStart,
      kReaderTextWidth, kReaderLinesPerPage, epub_text::TextStyle::Regular,
      false, readerCharacterWidth);
}

uint32_t readerPageNumber() {
  uint32_t pageNumber = 1;
  size_t offset = 0;
  epub_text::TextStyle style = epub_text::TextStyle::Regular;
  while (offset < readerPageStart) {
    const epub_text::TextPage page = epub_text::paginate(
        readerChapterText.c_str(), readerChapterText.length(), offset,
        kReaderTextWidth, kReaderLinesPerPage, style, true,
        readerCharacterWidth);
    if (page.end <= offset || page.end > readerPageStart) break;
    offset = page.end;
    style = page.finalStyle;
    ++pageNumber;
  }
  return pageNumber;
}

bool readerHasCover() {
  return epubArchive.hasCover() || !readerFolderCoverPath.isEmpty();
}

String findReaderFolderCoverPath(const String& bookPath) {
  constexpr const char* kCoverNames[] = {"cover.png", "cover.jpg"};
  for (const char* coverName : kCoverNames) {
    const String candidate =
        epub_cover::folderCoverPath(bookPath.c_str(), coverName).c_str();
    if (!sd_card::fileExists(candidate)) continue;

    File cover = sd_card::openForRead(candidate);
    if (!cover) {
      LOG.printf("[games] could not open folder cover %s\n",
                 candidate.c_str());
      continue;
    }
    const size_t length = cover.size();
    cover.close();
    if (length == 0 || length > EpubArchive::kMaximumCoverBytes) {
      LOG.printf("[games] ignoring folder cover %s (%lu bytes)\n",
                 candidate.c_str(), static_cast<unsigned long>(length));
      continue;
    }
    return candidate;
  }
  return "";
}

bool loadReaderFolderCoverImage(RgbImage& image) {
  if (readerFolderCoverPath.isEmpty() || !sdCardInserted()) return false;

  // Avoid a recovery remount while the EPUB archive has an open File handle.
  File cover = SD.open(readerFolderCoverPath, FILE_READ);
  if (!cover) {
    LOG.printf("[games] could not reopen folder cover %s\n",
               readerFolderCoverPath.c_str());
    return false;
  }
  const size_t length = cover.size();
  if (length == 0 || length > EpubArchive::kMaximumCoverBytes) {
    cover.close();
    LOG.printf("[games] refusing changed folder cover %s (%lu bytes)\n",
               readerFolderCoverPath.c_str(),
               static_cast<unsigned long>(length));
    return false;
  }

  uint8_t* data = static_cast<uint8_t*>(ps_malloc(length));
  if (data == nullptr) data = static_cast<uint8_t*>(malloc(length));
  if (data == nullptr) {
    cover.close();
    LOG.println("[games] folder cover allocation failed");
    return false;
  }
  size_t bytesRead = 0;
  while (bytesRead < length) {
    const size_t chunk = cover.read(data + bytesRead, length - bytesRead);
    if (chunk == 0) break;
    bytesRead += chunk;
  }
  cover.close();
  if (bytesRead != length) {
    free(data);
    LOG.println("[games] folder cover read failed");
    return false;
  }

  const bool decoded =
      load_image_from_memory(data, length, readerFolderCoverPath.c_str(), 0, 0,
                             &image);
  free(data);
  return decoded;
}

bool renderEpubCoverImage() {
  RgbImage image;
  bool decoded = false;
  if (epubArchive.hasCover()) {
    EpubCoverData cover;
    if (!epubArchive.loadCover(cover)) {
      LOG.printf("[games] could not extract EPUB cover: %s\n",
                 epubArchive.error().c_str());
      return false;
    }
    LOG.printf("[games] decoding EPUB cover %s (%lu bytes)\n",
               cover.nameHint().c_str(),
               static_cast<unsigned long>(cover.length()));
    decoded = load_image_from_memory(cover.data(), cover.length(),
                                     cover.nameHint().c_str(),
                                     kReaderCoverMaximumWidth,
                                     kReaderCoverMaximumHeight, &image);
    cover.clear();
  } else if (!readerFolderCoverPath.isEmpty()) {
    LOG.printf("[games] decoding folder cover %s\n",
               readerFolderCoverPath.c_str());
    decoded = loadReaderFolderCoverImage(image);
  } else {
    return false;
  }

  if (!decoded || image.width <= 0 || image.height <= 0) {
    image_free(&image);
    LOG.println("[games] EPUB cover decode failed");
    return false;
  }

  const int sourceWidth = image.width;
  const int sourceHeight = image.height;
  const float scale =
      std::min(static_cast<float>(kReaderCoverMaximumWidth) / sourceWidth,
               static_cast<float>(kReaderCoverMaximumHeight) / sourceHeight);
  const int targetWidth =
      std::max(1, std::min(kReaderCoverMaximumWidth,
                           static_cast<int>(sourceWidth * scale)));
  const int targetHeight =
      std::max(1, std::min(kReaderCoverMaximumHeight,
                           static_cast<int>(sourceHeight * scale)));
  const size_t pixelCount =
      static_cast<size_t>(targetWidth) * targetHeight;
  uint8_t* indices = static_cast<uint8_t*>(ps_malloc(pixelCount));
  if (indices == nullptr) indices = static_cast<uint8_t*>(malloc(pixelCount));
  if (indices == nullptr) {
    image_free(&image);
    LOG.println("[games] EPUB cover index allocation failed");
    return false;
  }

  const bool dithered = dither_resized_image(
      image.pixels, sourceWidth, sourceHeight, targetWidth, targetHeight,
      PAL_BW, 1.0f, false, indices);
  image_free(&image);
  if (!dithered) {
    free(indices);
    LOG.println("[games] EPUB cover dithering failed");
    return false;
  }

  const size_t packedBytes =
      static_cast<size_t>((targetWidth + 7) / 8) * targetHeight;
  uint8_t* packed = static_cast<uint8_t*>(ps_malloc(packedBytes));
  if (packed == nullptr) packed = static_cast<uint8_t*>(malloc(packedBytes));
  if (packed == nullptr) {
    free(indices);
    LOG.println("[games] EPUB cover bitmap allocation failed");
    return false;
  }
  pack_1bpp_msb(indices, packed, targetWidth, targetHeight, true);
  free(indices);

  const int targetX = (kScreenWidth - targetWidth) / 2;
  const int targetY =
      kReaderCoverTop + (kReaderCoverMaximumHeight - targetHeight) / 2;
  epaper.drawBitmap(targetX, targetY, packed, targetWidth, targetHeight,
                    TFT_BLACK, TFT_WHITE);
  free(packed);
  LOG.printf("[games] EPUB cover rendered %dx%d -> %dx%d\n", sourceWidth,
             sourceHeight, targetWidth, targetHeight);
  return true;
}

void drawEpubCover() {
  epaper.fillSprite(TFT_WHITE);
  if (!renderEpubCoverImage()) {
    constexpr int kFallbackLeft = 100;
    constexpr int kFallbackTop = 180;
    constexpr int kFallbackWidth = 280;
    constexpr int kFallbackHeight = 400;
    epaper.drawRoundRect(kFallbackLeft, kFallbackTop, kFallbackWidth,
                        kFallbackHeight, 10, TFT_BLACK);
    epaper.drawRoundRect(kFallbackLeft + 2, kFallbackTop + 2,
                        kFallbackWidth - 4, kFallbackHeight - 4, 8, TFT_BLACK);
    drawCentered("EPUB", kScreenWidth / 2,
                 kFallbackTop + kFallbackHeight / 2, 4);
  }
  drawGameStatusBar(tr(TextId::EpubReader));
}

void drawEpubReading() {
  if (readerCoverVisible) {
    drawEpubCover();
    return;
  }
  epaper.fillSprite(TFT_WHITE);
  const epub_text::TextPage page = currentReaderPage();
  const bool bodyUsesEmbeddedLatin = pageUsesEmbeddedReaderFont(page);
  const bool bodyRequiresCjk = pageRequiresCjk(page);
  const bool titleUsesEmbeddedLatin = usesEmbeddedReaderFont(
      epubArchive.title().c_str(), epubArchive.title().length());
  const bool titleRequiresCjk =
      epub_text::containsCjk(epubArchive.title().c_str(),
                             epubArchive.title().length());
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(MC_DATUM);

  bool bodyFontReady = true;
  bool titleFontReady = true;
  bool embeddedFontLoaded = false;
  epub_text::TextStyle loadedStyle = epub_text::TextStyle::Regular;
  if (!bodyUsesEmbeddedLatin) {
    bodyFontReady = loadReaderFont(24, bodyRequiresCjk || titleRequiresCjk);
    titleFontReady = !titleRequiresCjk || bodyFontReady;
  } else if (titleUsesEmbeddedLatin) {
    epaper.loadFont(epub_latin_fonts::kRegular);
    embeddedFontLoaded = true;
  } else {
    titleFontReady = loadReaderFont(24, titleRequiresCjk);
  }
  if (titleFontReady) {
    drawReaderString(
        fitReaderText(epubArchive.title(), kScreenWidth - 36),
        kScreenWidth / 2, 76);
  }

  epaper.setTextDatum(TL_DATUM);
  epub_text::TextStyle style = page.initialStyle;
  if (bodyFontReady) {
    if (bodyUsesEmbeddedLatin && !titleUsesEmbeddedLatin) {
      epaper.unloadFont();
      embeddedFontLoaded = false;
    }
    for (size_t index = 0; index < page.lines.size(); ++index) {
      drawStyledReaderLine(
          page.lines[index], 18,
          104 + static_cast<int>(index) * kReaderLineHeight, style,
          bodyUsesEmbeddedLatin, loadedStyle, embeddedFontLoaded,
          index < page.justifyLines.size() && page.justifyLines[index]);
    }
  }
  epaper.unloadFont();
  if ((bodyRequiresCjk && !bodyFontReady) ||
      (titleRequiresCjk && !titleFontReady)) {
    drawCentered(tr(TextId::CjkFontRequired), kScreenWidth / 2, 360, 4);
  }

  const String location =
      String(tr(TextId::Chapter)) + " " + String(readerChapterIndex + 1) +
      " / " + String(epubArchive.chapterCount()) + "   " +
      tr(TextId::Page) + " " + String(readerPageNumber());
  drawCentered(location, kScreenWidth / 2, 776, 2);
  drawGameStatusBar(tr(TextId::EpubReader));
}

bool listReaderBrowserPath(const String& path, bool resetPage) {
  const int savedPageStart = resetPage ? 0 : browserPageStart;
  readerBrowserPath = path.isEmpty() ? "/" : path;
  if (!sdCardReady) return false;
  const uint32_t startedAtMs = millis();
  if (sdBrowser.open(readerBrowserPath)) {
    browserPageStart = savedPageStart;
    LOG.printf("[games] listed %s: %d entries in %lu ms%s\n",
               readerBrowserPath.c_str(), sdBrowser.count(),
               static_cast<unsigned long>(millis() - startedAtMs),
               sdBrowser.truncated() ? " (truncated)" : "");
    return true;
  }
  LOG.printf("[games] could not open SD directory: %s\n",
             readerBrowserPath.c_str());
  readerBrowserPath = "/";
  browserPageStart = 0;
  browserMessage = tr(TextId::OpenFailed);
  if (sdBrowser.open(readerBrowserPath)) return true;

  clearReaderStorageState();
  SD.end();
  sdCardReady = false;
  return false;
}

bool openReaderBrowserPath(const String& path) {
  epubArchive.close();
  readerChapterText.clear();
  readerChapterRequiresNonAscii = false;
  readerChapterRequiresCjk = false;
  readerBookPath = "";
  readerFolderCoverPath = "";
  readerPageStart = 0;
  readerChapterIndex = 0;
  readerCoverVisible = false;
  browserMessage = "";
  return listReaderBrowserPath(path, true);
}

bool loadReaderChapter(int chapter, size_t pageStart = 0) {
  EpubChapterText text;
  if (!epubArchive.loadChapter(chapter, text)) return false;
  readerChapterText = std::move(text);
  readerChapterRequiresNonAscii =
      containsNonAscii(readerChapterText.c_str(),
                       readerChapterText.length());
  readerChapterRequiresCjk =
      epub_text::containsCjk(readerChapterText.c_str(),
                             readerChapterText.length());
  readerChapterIndex = chapter;
  readerPageStart =
      pageStart < readerChapterText.length() ? pageStart : 0;
  readerCoverVisible = false;
  return true;
}

bool openReaderBook(const String& path, int chapter = 0,
                    size_t pageStart = 0, bool skipUnreadable = true,
                    bool showCover = true) {
  readerChapterText.clear();
  readerChapterRequiresNonAscii = false;
  readerChapterRequiresCjk = false;
  readerCoverVisible = false;
  readerFolderCoverPath = "";
  LOG.printf(
      "[games] opening EPUB %s (heap=%lu KiB, PSRAM=%lu KiB)\n",
      path.c_str(), static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
      static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  if (!sdCardReady) {
    LOG.println("[games] cannot open EPUB without an SD card");
    return false;
  }
  detectReaderFonts();
  const String folderCoverPath = findReaderFolderCoverPath(path);
  if (!epubArchive.open(path)) {
    LOG.printf("[games] could not open EPUB %s: %s\n", path.c_str(),
               epubArchive.error().c_str());
    return false;
  }
  LOG.printf("[games] EPUB archive ready: %d spine items\n",
             epubArchive.chapterCount());
  const int firstChapter =
      std::max(0, std::min(chapter, epubArchive.chapterCount() - 1));
  const int lastChapter =
      skipUnreadable ? epubArchive.chapterCount() : firstChapter + 1;
  for (int index = firstChapter; index < lastChapter; ++index) {
    if (!loadReaderChapter(index, index == firstChapter ? pageStart : 0)) {
      LOG.printf("[games] skipping EPUB chapter %d: %s\n", index + 1,
                 epubArchive.error().c_str());
      continue;
    }
    readerBookPath = path;
    if (!epubArchive.hasCover()) {
      readerFolderCoverPath = folderCoverPath;
    }
    readerCoverVisible = showCover && readerHasCover();
    LOG.printf(
        "[games] EPUB ready: chapter=%d bytes=%lu cover=%s heap=%lu KiB "
        "PSRAM=%lu KiB\n",
        readerChapterIndex + 1,
        static_cast<unsigned long>(readerChapterText.length()),
        readerCoverVisible ? "yes" : "no",
        static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
        static_cast<unsigned long>(ESP.getFreePsram() / 1024));
    return true;
  }
  const String savedError = epubArchive.error();
  epubArchive.close();
  LOG.printf("[games] EPUB has no readable chapter: %s\n",
             savedError.c_str());
  return false;
}

bool recoverFullRefresh() {
  epaper.sleep();
  epaper.update();
  const E1005FastRefresh::Result result = fastRefresh.begin();
  if (result == E1005FastRefresh::Result::Ok) return true;
  LOG.printf("[games] cannot restore fast refresh: %s\n",
             E1005FastRefresh::resultMessage(result));
  touchReady = false;
  return false;
}

bool refreshRegion(const E1005FastRefresh::Region& region,
                   const char* action) {
  E1005FastRefresh::Timing timing;
  const E1005FastRefresh::Result result = fastRefresh.refresh(region, timing);
  if (result != E1005FastRefresh::Result::Ok) {
    LOG.printf("[games] %s refresh failed: %s\n", action,
               E1005FastRefresh::resultMessage(result));
    return recoverFullRefresh();
  }
  LOG.printf(
      "[games] %s refresh=%lu us "
      "(prepare=%lu transfer=%lu panel=%lu reseed=%lu)\n",
      action, static_cast<unsigned long>(timing.totalUs),
      static_cast<unsigned long>(timing.prepareUs),
      static_cast<unsigned long>(timing.transferUs),
      static_cast<unsigned long>(timing.panelUs),
      static_cast<unsigned long>(timing.reseedUs));
  return true;
}

bool refreshScreen(const char* action) {
  const uint32_t startedAtMs = millis();
  epaper.sleep();
  epaper.update();
  const E1005FastRefresh::Result result = fastRefresh.begin();
  if (result != E1005FastRefresh::Result::Ok) {
    LOG.printf("[games] %s full refresh recovery failed: %s\n", action,
               E1005FastRefresh::resultMessage(result));
    touchReady = false;
    return false;
  }
  LOG.printf("[games] %s full refresh=%lu ms\n", action,
             static_cast<unsigned long>(millis() - startedAtMs));
  return true;
}

void drawCurrentScreen();

void toggleHelpPane() {
  helpPaneVisible = !helpPaneVisible;
  if (helpPaneVisible) {
    drawHelpPane();
    refreshScreen("help pane opened");
  } else {
    drawCurrentScreen();
    refreshScreen("help pane closed");
  }
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case Screen::Menu:
      drawMenu();
      return;
    case Screen::EpubBrowser:
      drawEpubBrowser();
      return;
    case Screen::EpubReading:
      drawEpubReading();
      return;
  }
}

void showLanguageSelection() {
  saveResumeState();
  helpPaneVisible = false;
  languageSelectionVisible = true;
  drawLanguageSelection();
  refreshScreen("language selection");
}

void selectLanguage(Language language) {
  const game_language_store::Status status =
      game_language_store::save(language);
  if (status != game_language_store::Status::Ok) {
    LOG.printf("[games] could not save language: %s\n",
               game_language_store::statusMessage(status));
    return;
  }

  currentLanguage = language;
  languageSelected = true;
  languageSelectionVisible = false;
  LOG.printf("[games] language selected: %s\n",
             game_localization::languageName(language));
  drawCurrentScreen();
  refreshScreen("language changed");
}

void showMenu() {
  const GameId returningGame = gameForScreen(currentScreen);
  helpPaneVisible = false;
  if (currentScreen == Screen::EpubBrowser ||
      currentScreen == Screen::EpubReading) {
    epubArchive.close();
    readerChapterText.clear();
    readerChapterRequiresNonAscii = false;
    readerChapterRequiresCjk = false;
  }
  if (returningGame != GameId::Count) {
    currentMenuPage = menuPageForGame(returningGame);
  }
  currentScreen = Screen::Menu;
  saveResumeState();
  drawMenu();
  refreshScreen("menu");
}

void showEpubBrowser(bool fullRefresh = true) {
  currentScreen = Screen::EpubBrowser;
  const ReaderStorageStatus storage = refreshReaderStorage();
  if (storage != ReaderStorageStatus::Missing) {
    openReaderBrowserPath(readerBrowserPath);
  }
  saveResumeState();
  drawEpubBrowser();
  if (fullRefresh) {
    refreshScreen("EPUB browser");
  } else {
    refreshRegion(kReaderRegion, "EPUB browser");
  }
}

void renderDashboardScreen() {
  dashboard_render::SmoothFont font(epaper);
  const bool smoothOk = sdCardReady && font.load(dashboard_render::FontSize::Small);
  if (!smoothOk) font.selectGfxFallback(dashboard_render::FontSize::Small);
  dashboard_render::renderScreen(
      epaper, font,
      static_cast<dashboard_render::Screen>(
          static_cast<int>(runners_journal::currentDashboardScreen)),
      runners_journal::dashboardData);
  font.unload();
}

void renderStatusScreenAndRefresh() {
  dashboard_render::SmoothFont font(epaper);
  const bool smoothOk = sdCardReady && font.load(dashboard_render::FontSize::Small);
  if (!smoothOk) font.selectGfxFallback(dashboard_render::FontSize::Small);
  dashboard_render::renderStatus(
      epaper, font, "Løp",
      "Kunne ikke hente data. OK-knappen = prøv igjen. Hold OK inne = "
      "Wi-Fi-innstillinger (QR).");
  font.unload();
  refreshScreen("Runners Journal status");
}

void showRunnersJournal() {
  LOG.println("[runners-journal] Launching Løp...");
  currentScreen = Screen::RunnersJournal;
  runners_journal::lastActivityTime = millis();

  // Suppress timer-wakes while in runners-journal
  runners_journal::timerWakesSuppressed = true;

  // Fetch dashboard data if not already fetched
  if (!runners_journal::dataFetched) {
    fetchRunnersJournalData();
  }

  if (!runners_journal::dataFetched) {
    LOG.println("[runners-journal] fetch failed; showing status");
    runners_journal::showingFetchStatus = true;
    renderStatusScreenAndRefresh();
    handleRunnersJournalInput();
    return;
  }

  runners_journal::showingFetchStatus = false;
  renderDashboardScreen();
  refreshScreen("Runners Journal");

  // Enter the runners-journal input loop
  handleRunnersJournalInput();
}

void runConfigPortalAndReboot();

void runConfigPortalAndReboot() {
  LOG.println("[portal] entering config portal");
  disableLightSleepWake();
  const uint32_t drawStart = millis();

  config_portal::Config portalCfg;
  portalCfg.wifiSchema = &config_portal::kWifiSchema;
  portalCfg.appName = "Runners Journal";
  portalCfg.useAutoApPassword = true;
  portalCfg.wifiFallback = [](const char* key) -> String {
    if (strcmp(key, "ssid") == 0) return String(sticky_wifi::ssid());
    if (strcmp(key, "password") == 0) return String(sticky_wifi::password());
    return String();
  };

  if (!config_portal::begin(portalCfg)) {
    LOG.println("[portal] begin failed; rebooting");
    delay(250);
    ESP.restart();
  }

  config_portal::ui::RenderInfo info;
  info.modelLabel = "reTerminal E1005";
  info.title = "Runners Journal";
  info.tagline = "Koble til for å sette Wi-Fi";
  info.ssid = config_portal::currentSsid();
  info.wifiPassword = config_portal::currentApPassword();
  info.url = String("http://") + config_portal::currentIp().toString();
  info.macAddress = WiFi.macAddress();
  info.wifiPayload = config_portal::wifiQrPayload(
      info.ssid, info.wifiPassword.length() ? info.wifiPassword.c_str() : nullptr);
  info.urlPayload = config_portal::urlQrPayload(
      config_portal::currentIp(), config_portal::currentPort(), "/wifi");
  info.footerHint = "OK-knapp = start på nytt";
  info.fonts.titleFont = &FreeSansBold9pt7b;
  info.fonts.subtitleFont = &FreeSansBold9pt7b;
  info.fonts.captionFont = &FreeSansBold9pt7b;
  info.fonts.detailFont = &FreeSansBold9pt7b;

  config_portal::ui::renderPortalScreen<EPaper>(
      epaper, kScreenWidth, kScreenHeight, TFT_BLACK, TFT_WHITE, info);
  LOG.printf("[portal] splash drawn in %u ms\n",
             static_cast<unsigned>(millis() - drawStart));
  epaper.update();
  LOG.println("[portal] splash committed; serving Wi-Fi portal");

  while (!config_portal::rebootRequested()) {
    config_portal::loop();
    if (digitalRead(board::PIN_BUTTON_0) == LOW) {
      hardware::beep();
      LOG.println("[portal] OK pressed; rebooting");
      delay(200);
      ESP.restart();
    }
    delay(2);
  }
  LOG.println("[portal] reboot requested; restarting");
  delay(250);
  ESP.restart();
}

void fetchRunnersJournalData() {
  LOG.println("[runners-journal] Fetching dashboard data...");
  
  if (!sticky_wifi::haveCredentials()) {
    LOG.println("[runners-journal] no Wi-Fi credentials; launching config portal");
    runners_journal::dataFetched = false;
    runConfigPortalAndReboot();
    return;
  }
  
  String wifiFailure;
  const wifi_sta::ConnectResult wifiResult = wifi_sta::connectStation(
      sticky_wifi::ssid(), sticky_wifi::password(),
      config::WIFI_CONNECT_TIMEOUT_MS, &wifiFailure
  );
  
  if (!wifiResult.connected) {
    LOG.printf("[runners-journal] WiFi connect failed: %s\n", wifiFailure.c_str());
    runners_journal::dataFetched = false;
    return;
  }
  
  LOG.printf("[runners-journal] WiFi connected, IP %s\n", WiFi.localIP().toString().c_str());
  // Sync NTP once per boot while the radio is already up; persists to the
  // PCF8563 so the clock survives the next deep sleep. Skipped whenever the
  // clock is already valid (RTC restore or a previous sync this boot).
  if (!local_time::clockIsValid()) {
    LOG.println("[ntp] clock invalid; synchronizing from NTP");
    ntp::synchronizeAndPersist(config::TIMEZONE, "pool.ntp.org", "time.cloudflare.com",
                               config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS,
                               nullptr);
  }
  
  // Fetch dashboard data
  String body;
  String fetchFailure;
  if (!dashboard_fetch::fetch(body, fetchFailure)) {
    LOG.printf("[runners-journal] Fetch failed: %s\n", fetchFailure.c_str());
    wifi_sta::disable();
    runners_journal::dataFetched = false;
    return;
  }
  
  LOG.printf("[runners-journal] Fetched %u bytes\n", body.length());
  
  // Parse dashboard data
  if (!dashboard::parse(body, runners_journal::dashboardData)) {
    LOG.println("[runners-journal] Parse failed");
    wifi_sta::disable();
    runners_journal::dataFetched = false;
    return;
  }
  
  LOG.printf("[runners-journal] Parsed data: uke=%s total_km=%.1f\n",
             runners_journal::dashboardData.uke.merkelapp.c_str(),
             runners_journal::dashboardData.uke.total_km);
  
  runners_journal::dataFetched = true;
  wifi_sta::disable();
}

void handleRunnersJournalInput() {
  while (currentScreen == Screen::RunnersJournal) {
    // Check for idle timeout (4 minutes)
    if (millis() - runners_journal::lastActivityTime > 4 * 60 * 1000) {
      LOG.println("[runners-journal] Idle timeout, sleeping...");
      powerDownAndSleep(SleepScreen::Resume, -1);
      return;
    }

    // Poll for touch/buttons
    pollTouch();
    ButtonEvent event = {};
    if (pollButtonEvent(event)) {
      handleRunnersJournalButton(event);
    }

    // Buttons are polled; 25 ms is far below the panel's multi-second
    // refresh latency but quarters the busy-loop wakeups.
    delay(25);
  }
}

void handleRunnersJournalButton(const ButtonEvent& event) {
  if (event.heldMs == 0) return;
  
  runners_journal::lastActivityTime = millis();
  
  if (event.button->pin == board::PIN_BUTTON_1) {
    // UP: Previous screen (wrap around)
    int next = static_cast<int>(runners_journal::currentDashboardScreen);
    next = (next + kRunnersJournalScreenCount - 1) % kRunnersJournalScreenCount;
    runners_journal::currentDashboardScreen = static_cast<RunnersJournalScreen>(next);
    renderDashboardScreen();
    refreshScreen("Runners Journal");
    LOG.printf("[runners-journal] UP pressed, screen=%d\n", next);
    return;
  }
  
  if (event.button->pin == board::PIN_BUTTON_2) {
    // DOWN: Next screen (wrap around)
    int next = static_cast<int>(runners_journal::currentDashboardScreen);
    next = (next + 1) % kRunnersJournalScreenCount;
    runners_journal::currentDashboardScreen = static_cast<RunnersJournalScreen>(next);
    renderDashboardScreen();
    refreshScreen("Runners Journal");
    LOG.printf("[runners-journal] DOWN pressed, screen=%d\n", next);
    return;
  }
  
  if (event.button->pin == board::PIN_BUTTON_0) {
    if (event.heldMs >= ok_button::kDeepSleepHoldMs) {
      if (runners_journal::showingFetchStatus) {
        // OK long-press on the fetch-failed screen: reopen the Wi-Fi
        // portal so stored credentials can be replaced without a PC.
        LOG.println("[runners-journal] OK long-press on status; opening portal");
        runners_journal::showingFetchStatus = false;
        runners_journal::timerWakesSuppressed = false;
        runConfigPortalAndReboot();
        return;
      }
      // OK long-press: exit to the game selector
      LOG.println("[runners-journal] OK long-press, exiting to selector");
      runners_journal::timerWakesSuppressed = false;
      currentScreen = Screen::Menu;
      showMenuPage(MenuPage::First, true);
      return;
    }
    // OK short-press: refetch dashboard data
    LOG.println("[runners-journal] OK short-press, refetching data");
    runners_journal::dataFetched = false;
    fetchRunnersJournalData();
    if (runners_journal::dataFetched) {
      runners_journal::showingFetchStatus = false;
      renderDashboardScreen();
      refreshScreen("Runners Journal");
    } else {
      runners_journal::showingFetchStatus = true;
      renderStatusScreenAndRefresh();
    }
    return;
  }
}

void showEpubReading(bool fullRefresh = false) {
  currentScreen = Screen::EpubReading;
  saveResumeState();
  drawEpubReading();
  if (readerCoverVisible || fullRefresh) {
    refreshScreen(readerCoverVisible ? "EPUB cover" : "EPUB first text page");
  } else {
    refreshRegion(kReaderRegion, "EPUB page");
  }
}

bool prepareEpubBrowserInteraction() {
  const ReaderStorageStatus storage = refreshReaderStorage();
  if (storage == ReaderStorageStatus::Ready &&
      listReaderBrowserPath(readerBrowserPath, false)) {
    return true;
  }
  if (storage == ReaderStorageStatus::Changed) {
    openReaderBrowserPath("/");
  }
  currentScreen = Screen::EpubBrowser;
  saveResumeState();
  drawEpubBrowser();
  refreshScreen(storage == ReaderStorageStatus::Changed
                    ? "EPUB SD card changed"
                    : "EPUB SD card unavailable");
  return false;
}

bool prepareEpubReadingInteraction() {
  const ReaderStorageStatus storage = refreshReaderStorage();
  if (storage == ReaderStorageStatus::Ready && epubArchive.isOpen()) {
    return true;
  }
  showEpubBrowser();
  return false;
}

void showPreviousBrowserPage(bool validateStorage = true) {
  if (validateStorage && !prepareEpubBrowserInteraction()) return;
  if (!hasPreviousBrowserPage()) {
    LOG.println("[games] already on first EPUB browser page");
    return;
  }
  browserPageStart = std::max(0, browserPageStart - kBrowserRowsPerPage);
  browserMessage = "";
  drawEpubBrowser();
  refreshRegion(kReaderRegion, "EPUB previous files");
}

void showNextBrowserPage(bool validateStorage = true) {
  if (validateStorage && !prepareEpubBrowserInteraction()) return;
  if (!hasNextBrowserPage()) {
    LOG.println("[games] already on last EPUB browser page");
    return;
  }
  browserPageStart += kBrowserRowsPerPage;
  browserMessage = "";
  drawEpubBrowser();
  refreshRegion(kReaderRegion, "EPUB next files");
}

void launchGame(GameId game) {
  hardware::beep();
  switch (game) {
    case GameId::EpubReader:
      showEpubBrowser();
      break;
    case GameId::RunnersJournal:
      showRunnersJournal();
      break;
    case GameId::Count:
      return;
  }
  saveResumeState();
}

void showMenuPage(MenuPage page, bool beep = true) {
  if (beep) hardware::beep();
  currentMenuPage = page;
  saveResumeState();
  drawMenu();
  refreshScreen("menu page");
}

void handleLanguageTouch(const Gt911Touch::Point& point) {
  for (size_t index = 0; index < game_localization::kLanguageCount; ++index) {
    if (!kLanguageButtons[index].contains(point.x, point.y)) continue;
    hardware::beep();
    selectLanguage(static_cast<Language>(index));
    return;
  }
}

void handleMenuTouch(const Gt911Touch::Point& point) {
  if (kSettingsButton.contains(point.x, point.y)) {
    hardware::beep();
    showLanguageSelection();
    return;
  }
  const size_t pageIndex = static_cast<size_t>(currentMenuPage);
  if (pageIndex + 1 < kMenuPageCount &&
      kNextPageButton.contains(point.x, point.y)) {
    showMenuPage(static_cast<MenuPage>(pageIndex + 1));
    return;
  }
  if (pageIndex > 0 &&
      kPreviousPageButton.contains(point.x, point.y)) {
    showMenuPage(static_cast<MenuPage>(pageIndex - 1));
    return;
  }
  const size_t firstPosition = pageIndex * kGamesPerMenuPage;
  const size_t visibleGames =
      std::min(kGamesPerMenuPage, kGameCount - firstPosition);
  for (size_t slot = 0; slot < visibleGames; ++slot) {
    if (kMenuCardSlots[slot].contains(point.x, point.y)) {
      launchGame(orderedGameAt(firstPosition + slot));
      return;
    }
  }
}

bool handleMenuEdgeSwipe(const Gt911Touch::Point& start,
                         const Gt911Touch::Point& end) {
  const menu_edge_swipe::Direction direction = menu_edge_swipe::detect(
      start.x, start.y, end.x, end.y, kScreenWidth, kMenuSwipeEdgeWidth,
      kSwipeThreshold);
  const size_t pageIndex = static_cast<size_t>(currentMenuPage);
  if (direction == menu_edge_swipe::Direction::Previous) {
    if (pageIndex > 0) {
      showMenuPage(static_cast<MenuPage>(pageIndex - 1));
    } else {
      LOG.println("[games] already on first menu page");
    }
    return true;
  }
  if (direction == menu_edge_swipe::Direction::Next) {
    if (pageIndex + 1 < kMenuPageCount) {
      showMenuPage(static_cast<MenuPage>(pageIndex + 1));
    } else {
      LOG.println("[games] already on last menu page");
    }
    return true;
  }
  return false;
}

int absoluteDistance(int first, int second) {
  const int difference = first - second;
  return difference < 0 ? -difference : difference;
}

size_t lastReaderPageStart() {
  size_t offset = 0;
  size_t last = 0;
  epub_text::TextStyle style = epub_text::TextStyle::Regular;
  while (offset < readerChapterText.length()) {
    const epub_text::TextPage page = epub_text::paginate(
        readerChapterText.c_str(), readerChapterText.length(), offset,
        kReaderTextWidth, kReaderLinesPerPage, style, true,
        readerCharacterWidth);
    if (page.end <= offset || page.end >= readerChapterText.length()) {
      return last;
    }
    last = page.end;
    offset = page.end;
    style = page.finalStyle;
  }
  return last;
}

void showPreviousReaderPage() {
  if (!prepareEpubReadingInteraction()) return;
  if (readerCoverVisible) {
    LOG.println("[games] already on EPUB cover");
    return;
  }
  if (readerPageStart > 0) {
    readerPageStart = epub_text::previousPageStart(
        readerChapterText.c_str(), readerChapterText.length(), readerPageStart,
        kReaderTextWidth, kReaderLinesPerPage, readerCharacterWidth);
    showEpubReading();
    return;
  }
  for (int chapter = readerChapterIndex - 1; chapter >= 0; --chapter) {
    if (!loadReaderChapter(chapter)) continue;
    readerPageStart = lastReaderPageStart();
    showEpubReading();
    return;
  }
  if (readerHasCover()) {
    readerCoverVisible = true;
    showEpubReading();
  }
}

void showNextReaderPage() {
  if (!prepareEpubReadingInteraction()) return;
  if (readerCoverVisible) {
    readerCoverVisible = false;
    showEpubReading(true);
    return;
  }
  const epub_text::TextPage page = currentReaderPage();
  if (page.end < readerChapterText.length()) {
    readerPageStart = page.end;
    showEpubReading();
    return;
  }
  for (int chapter = readerChapterIndex + 1;
       chapter < epubArchive.chapterCount(); ++chapter) {
    if (!loadReaderChapter(chapter)) continue;
    showEpubReading();
    return;
  }
}

void handleEpubBrowserTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (!prepareEpubBrowserInteraction()) return;
  if (kPreviousPageButton.contains(point.x, point.y) &&
      hasPreviousBrowserPage()) {
    hardware::beep();
    showPreviousBrowserPage(false);
    return;
  }
  if (kNextPageButton.contains(point.x, point.y) && hasNextBrowserPage()) {
    hardware::beep();
    showNextBrowserPage(false);
    return;
  }
  if (point.y < kBrowserRowTop ||
      point.y >= kBrowserRowTop +
                     kBrowserRowsPerPage * kBrowserRowHeight) {
    return;
  }
  const int row = (point.y - kBrowserRowTop) / kBrowserRowHeight;
  const int itemIndex = browserPageStart + row;
  const bool hasParentFolder =
      epub_browser_logic::hasParentFolder(readerBrowserPath.c_str());
  if (epub_browser_logic::isParentFolderItem(itemIndex, hasParentFolder)) {
    hardware::beep();
    openReaderBrowserPath(SdReadonlyBrowser::parentPath(readerBrowserPath));
    drawEpubBrowser();
    refreshRegion(kReaderRegion, "EPUB parent folder");
    return;
  }
  const SdReadonlyBrowser::Entry* entry =
      sdBrowser.entry(
          epub_browser_logic::storedEntryIndex(itemIndex, hasParentFolder));
  if (entry == nullptr) return;
  if (entry->directory) {
    hardware::beep();
    openReaderBrowserPath(entry->path);
    drawEpubBrowser();
    refreshRegion(kReaderRegion, "EPUB folder");
    return;
  }
  if (!entry->epub) return;

  hardware::beep();
  browserMessage = "";
  if (openReaderBook(entry->path)) {
    showEpubReading();
  } else {
    browserMessage = tr(TextId::OpenFailed);
    drawEpubBrowser();
    refreshRegion(kReaderRegion, "EPUB open failed");
  }
}

void handleEpubReadingTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showEpubBrowser(false);
    return;
  }
  if (readerCoverVisible) {
    readerCoverVisible = false;
    showEpubReading(true);
  }
}

void pollTouch() {
  if (!touchReady) return;

  Gt911Touch::Point point = {};
  const Gt911Touch::PollResult result = touch.poll(point);
  if (result == Gt911Touch::PollResult::Release) {
    if (touchActive && currentScreen == Screen::Menu &&
        !touchActionHandled) {
      if (!handleMenuEdgeSwipe(touchStart, touchLast)) {
        handleMenuTouch(touchStart);
      }
    } else if (touchActive && currentScreen == Screen::EpubReading &&
               !touchActionHandled) {
      const int dx = static_cast<int>(touchLast.x) -
                     static_cast<int>(touchStart.x);
      const int dy = static_cast<int>(touchLast.y) -
                     static_cast<int>(touchStart.y);
      if (menu_edge_swipe::absolute(dx) >= kSwipeThreshold &&
          menu_edge_swipe::absolute(dx) > menu_edge_swipe::absolute(dy)) {
        if (dx < 0) {
          showNextReaderPage();
          LOG.println("[epub] swipe left -> next page");
        } else {
          showPreviousReaderPage();
          LOG.println("[epub] swipe right -> previous page");
        }
      } else {
        handleEpubReadingTouch(touchStart);
      }
    }
    touchActive = false;
    touchActionHandled = false;
    return;
  }
  if (result != Gt911Touch::PollResult::Touch) return;
  recordActivity();
  if (touchActive) {
    touchLast = point;
    return;
  }

  touchActive = true;
  touchActionHandled = false;
  touchStart = point;
  touchLast = point;
  touchStartedAtMs = millis();
  if (languageSelectionVisible) {
    handleLanguageTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Menu) {
    if (!menu_edge_swipe::startsAtEdge(point.x, kScreenWidth,
                                       kMenuSwipeEdgeWidth)) {
      handleMenuTouch(point);
      touchActionHandled = true;
    }
  } else if (kHelpButton.contains(point.x, point.y)) {
    hardware::beep();
    toggleHelpPane();
    touchActionHandled = true;
  } else if (helpPaneVisible) {
    touchActionHandled = true;
  } else if (currentScreen == Screen::EpubBrowser) {
    handleEpubBrowserTouch(point);
    touchActionHandled = true;
  }
}

void powerDownAndSleep(SleepScreen screen = SleepScreen::Resume,
                       int batteryPercent = -1) {
  disableLightSleepWake();
  while (digitalRead(board::PIN_BUTTON_0) == LOW) delay(10);
  const bool wakePinReady = hardware::configureWakePin(board::PIN_BUTTON_0);
  const uint64_t wakeMask = 1ULL << board::PIN_BUTTON_0;
  const esp_err_t wakeResult =
      wakePinReady
          ? esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  LOG.printf("[games] wake config: %s\n", esp_err_to_name(wakeResult));
  if (wakeResult != ESP_OK) {
    LOG.println("[games] refusing deep sleep without an OK-button wake source");
    LOG.flush();
    delay(250);
    ESP.restart();
  }

  saveResumeState();
  if (screen == SleepScreen::Charge) {
    drawChargeSplash(batteryPercent);
  } else {
    drawSleepSplash();
  }
  LOG.printf("[games] entering deep sleep (%s)\n",
             screen == SleepScreen::Charge ? "low battery" : "requested");
  epaper.update();
  touch.end();
  fastRefresh.end();
  LOG.flush();
  delay(50);

  SD.end();
  epaper.getSPIinstance().end();
  pinMode(board::PIN_SD_CS, INPUT);
  pinMode(board::PIN_SD_SCK, INPUT);
  pinMode(board::PIN_SD_MOSI, INPUT);
  pinMode(board::PIN_SD_MISO, INPUT);
  peripheral_power::disableSd();
  peripheral_power::disable();
  power_latch::holdDuringDeepSleep();
  while (digitalRead(board::PIN_BUTTON_0) == LOW) delay(10);
  esp_deep_sleep_start();
}

void handleShortOkPress() {
  if (helpPaneVisible) {
    hardware::beep();
    toggleHelpPane();
    return;
  }
  if (languageSelectionVisible) {
    LOG.println("[games] no back action on language selection");
    return;
  }
  if (currentScreen == Screen::Menu) {
    LOG.println("[games] no back action on game picker");
    return;
  }

  hardware::beep();
  if (currentScreen == Screen::EpubReading) {
    showEpubBrowser(false);
  } else {
    showMenu();
  }
}

void handleButton(const ButtonEvent& event) {
  ButtonState& button = *event.button;
  recordActivity();
  LOG.printf("[games] %s released after %lu ms\n", button.name,
             static_cast<unsigned long>(event.heldMs));

  if (button.pin == board::PIN_BUTTON_0) {
    switch (ok_button::actionForHold(event.heldMs)) {
      case ok_button::Action::ShortPress:
        handleShortOkPress();
        return;
      case ok_button::Action::DeepSleep:
        // In reader modes, OK long-press exits to another screen
        if (currentScreen == Screen::EpubReading) {
          // Exit EPUB reader to RunnersJournal Screen 1 (Uke)
          LOG.println("[epub] OK long-press, exiting to runners-journal Screen 1");
          runners_journal::currentDashboardScreen = RunnersJournalScreen::Uke;
          runners_journal::timerWakesSuppressed = false; // Re-arm timer-wakes
          showRunnersJournal();
          return;
        } else if (currentScreen == Screen::RunnersJournal) {
          // Exit RunnersJournal to selector
          LOG.println("[runners-journal] OK long-press, exiting to selector");
          runners_journal::timerWakesSuppressed = false; // Re-arm timer-wakes
          showMenuPage(MenuPage::First, true);
          return;
        } else {
          // Default: Deep sleep
          hardware::beep();
          powerDownAndSleep();
          return;
        }
    }
  }

  hardware::beep();
  if (languageSelectionVisible) {
    LOG.println("[games] ignoring navigation button on language selection");
    return;
  }
  if (helpPaneVisible) {
    LOG.println("[games] ignoring navigation button while help is open");
    return;
  }
  if (button.pin == board::PIN_BUTTON_1) {
    if (currentScreen == Screen::EpubBrowser) {
      showPreviousBrowserPage();
    } else if (currentScreen == Screen::RunnersJournal) {
      // Handled in handleRunnersJournalButton()
      return;
    } else if (currentScreen != Screen::Menu) {
      showMenu();
    } else if (currentMenuPage != MenuPage::First) {
      showMenuPage(
          static_cast<MenuPage>(static_cast<uint8_t>(currentMenuPage) - 1),
          false);
    } else {
      LOG.println("[games] already on first menu page");
    }
    return;
  }
  if (button.pin == board::PIN_BUTTON_2) {
    if (currentScreen == Screen::EpubBrowser) {
      showNextBrowserPage();
    } else if (currentScreen == Screen::RunnersJournal) {
      // Handled in handleRunnersJournalButton()
      return;
    } else if (currentScreen == Screen::Menu &&
        static_cast<size_t>(currentMenuPage) + 1 < kMenuPageCount) {
      showMenuPage(
          static_cast<MenuPage>(static_cast<uint8_t>(currentMenuPage) + 1),
          false);
    } else if (currentScreen == Screen::Menu) {
      LOG.println("[games] already on last menu page");
    } else {
      LOG.println("[games] ignoring DOWN while playing");
    }
    return;
  }
  if (button.pin != board::PIN_BUTTON_0) {
    LOG.printf("[games] ignoring %s button\n", button.name);
    return;
  }
}

void checkBatteryAndSleepIfNeeded() {
  const uint32_t now = millis();
  if (nextBatteryCheckAtMs != 0 &&
      static_cast<int32_t>(now - nextBatteryCheckAtMs) < 0) {
    return;
  }
  nextBatteryCheckAtMs = now + kBatteryCheckIntervalMs;

  const battery::FuelGaugeReading gauge = battery::readBq27220();
  pinMode(board::PIN_EXTERNAL_POWER, INPUT);
  const bool externalPower =
      digitalRead(board::PIN_EXTERNAL_POWER) == HIGH;
  const int updatedPercent = gauge.valid ? gauge.percent : -1;
  const bool statusChanged =
      !batteryStatusSampled || batteryPercent != updatedPercent ||
      externalPowerPresent != externalPower;
  batteryStatusSampled = true;
  batteryPercent = updatedPercent;
  externalPowerPresent = externalPower;

  if (!gauge.valid) {
    LOG.println("[battery] BQ27220 battery gauge unavailable");
  } else {
    LOG.printf("[battery] %d%% (%.3fV), external_power=%s\n", gauge.percent,
               gauge.voltage, externalPower ? "yes" : "no");
    if (low_battery::shouldWarn(true, gauge.valid, externalPower,
                                gauge.percent, kLowBatteryThresholdPct)) {
      LOG.printf("[battery] below %d%%; requesting recharge\n",
                 kLowBatteryThresholdPct);
      powerDownAndSleep(SleepScreen::Charge, gauge.percent);
    }
  }

  if (statusChanged && fastRefresh.ready()) {
    epaper.fillRect(kBatteryStatusRegion.x, kBatteryStatusRegion.y,
                   kBatteryStatusRegion.width, kBatteryStatusRegion.height,
                   TFT_WHITE);
    drawBatteryStatus();
    refreshRegion(kBatteryStatusRegion, "battery status");
  }
}

void handleReaderCardRemoval() {
  if ((currentScreen != Screen::EpubBrowser &&
       currentScreen != Screen::EpubReading) ||
      !sdCardReady || sdCardInserted()) {
    return;
  }

  LOG.println("[games] SD card removed; closing EPUB reader");
  clearReaderStorageState();
  SD.end();
  sdCardReady = false;
  helpPaneVisible = false;
  currentScreen = Screen::EpubBrowser;
  saveResumeState();
  drawEpubBrowser();
  refreshScreen("EPUB SD card removed");
}

void sleepAfterInactivityIfNeeded() {
  // Use 4-minute timeout for reader modes, 5-minute otherwise
  const uint32_t timeoutMs = 
      (currentScreen == Screen::RunnersJournal ||
       currentScreen == Screen::EpubBrowser ||
       currentScreen == Screen::EpubReading) ?
      4UL * 60UL * 1000UL : kInactivitySleepMs;
  
  if (inputHandlingActive() ||
      static_cast<uint32_t>(millis() - lastActivityAtMs) < timeoutMs) {
    return;
  }
  LOG.printf("[games] %d minutes inactive; entering deep sleep\n", timeoutMs / (60 * 1000));
  powerDownAndSleep();
}

}  // namespace

void setup() {
  power_latch::holdOn();
  LOG.begin(115200, SERIAL_8N1, board::PIN_LOG_RX, board::PIN_LOG_TX);
  usbScreenCapture.begin(Serial1);
  delay(50);
  LOG.println();
  LOG.printf("[games] reTerminal E1005 %s\n", kAppName);
  hardware::beep();
  sticky_wifi::load();
  const game_language_store::LoadResult languageResult =
      game_language_store::load();
  if (languageResult.status == game_language_store::Status::Ok) {
    currentLanguage = languageResult.language;
    languageSelected = true;
    LOG.printf("[games] language: %s\n",
               game_localization::languageName(currentLanguage));
  } else {
    LOG.printf("[games] language selection required: %s\n",
               game_language_store::statusMessage(languageResult.status));
  }
  const bool resumed = restoreResumeState();
  LOG.printf("[games] boot mode: %s\n", resumed ? "resume" : "cold");
  // Restore the wall clock from the PCF8563 so timestamps (and TLS
  // certificate validation) work before the first NTP sync.
  rtc_sync::restoreSystemClock();

  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);

  epaper_setup::begin(epaper);
  checkBatteryAndSleepIfNeeded();
  sdCardReady = sd_card::mount(epaper.getSPIinstance(), "/sticky-arcade");
  if (sdCardReady && sd_ota::hasUpdate()) {
    drawStatus(tr(TextId::UpdatingFirmware), tr(TextId::DoNotPowerOff));
    epaper.update();
    const sd_ota::Result updateResult = sd_ota::apply();
    if (updateResult == sd_ota::Result::Applied) {
      delay(1000);
      ESP.restart();
    }
    drawStatus(tr(TextId::UpdateFailed), tr(TextId::CurrentFirmwareSafe));
    epaper.update();
    delay(2500);
  }
  const ReaderStorageStatus bootStorage = refreshReaderStorage();
  if (bootStorage != ReaderStorageStatus::Missing) detectReaderFonts();

  if (!resumed) currentScreen = Screen::Menu;
  if (resumed && currentScreen == Screen::EpubReading &&
      bootStorage == ReaderStorageStatus::Ready) {
    const String savedBookPath = readerBookPath;
    const int savedChapter = readerChapterIndex;
    const size_t savedPageStart = readerPageStart;
    const bool savedCoverVisible = readerCoverVisible;
    if (!openReaderBook(savedBookPath, savedChapter, savedPageStart, false,
                        savedCoverVisible)) {
      currentScreen = Screen::EpubBrowser;
      openReaderBrowserPath(readerBrowserPath);
      browserMessage = tr(TextId::OpenFailed);
    }
  } else if (resumed && currentScreen == Screen::EpubBrowser) {
    if (bootStorage == ReaderStorageStatus::Missing) {
      clearReaderStorageState();
    } else {
      openReaderBrowserPath(readerBrowserPath);
    }
  } else if (resumed && currentScreen == Screen::EpubReading) {
    currentScreen = Screen::EpubBrowser;
    if (bootStorage == ReaderStorageStatus::Changed) {
      openReaderBrowserPath("/");
    } else {
      clearReaderStorageState();
    }
  }
  if (languageSelected) {
    drawCurrentScreen();
  } else {
    languageSelectionVisible = true;
    drawLanguageSelection();
  }
  LOG.printf("[games] refreshing %s\n",
             languageSelectionVisible ? "language selection"
                                      : screenName(currentScreen));
  epaper.update();
  sd_ota::confirmRunningImage();

  const E1005FastRefresh::Result refreshResult = fastRefresh.begin();
  if (refreshResult != E1005FastRefresh::Result::Ok) {
    LOG.printf("[games] fast refresh unavailable: %s\n",
               E1005FastRefresh::resultMessage(refreshResult));
  }

  configureButtons();
  touchReady =
      refreshResult == E1005FastRefresh::Result::Ok && touch.begin(touchWire);
  if (touchReady) {
    LOG.printf("[touch] GT%s ready at 0x%02X, sensor=%ux%u\n",
               touch.productId(), touch.address(), touch.sensorWidth(),
               touch.sensorHeight());
  } else {
    LOG.println("[touch] GT911 initialization failed");
  }
  recordActivity();
  lightSleepReady = configureLightSleepWake();
  LOG.printf("[games] idle light sleep: %s\n",
             lightSleepReady ? "enabled" : "unavailable");
}

void loop() {
  usbScreenCapture.poll(epaper, kScreenWidth, kScreenHeight);
  handleReaderCardRemoval();
  checkBatteryAndSleepIfNeeded();
  pollTouch();
  ButtonEvent event = {};
  if (pollButtonEvent(event)) {
    handleButton(event);
  }
  sleepAfterInactivityIfNeeded();
  idleInLightSleep();
}
