#pragma once

// E1005 dashboard renderer for the runners-journal app. Monochrome
// 480x800 portrait. Uses the SD-backed smooth font (sans_bold_*.vlw) so
// Norwegian text (søndag, løp, æøå) renders correctly; falls back to a
// GFX bitmap font (ASCII-only) when the SD card is unavailable.
//
// The smooth-font path on the one-bit E1005 panel can't use TFT_eSPI's
// anti-aliased drawString() — it produces grey levels the panel can't
// show — so loaded glyphs are rasterised pixel-by-pixel with an alpha
// threshold (same approach as xkcd-viewer's drawLoadedSmoothTextMonochrome).

#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>

#include "runners_journal_config.h"
#include "dashboard_data.h"

namespace dashboard_render {

constexpr uint16_t INK = TFT_BLACK;           // svart
constexpr uint16_t PAPER = TFT_WHITE;         // hvit
constexpr uint16_t INK_SECONDARY = TFT_BLACK; // omrisset historisk stolpe
constexpr uint16_t INK_FAINT = TFT_BLACK;      // tynn skillelinje

constexpr int MARGIN = 16;
constexpr int LINE_GAP = 6;

enum class FontSize { Tiny, Small, Medium, Large, Huge };

enum class Screen { Uke, Aar, Siste, Journal };

constexpr int SCREEN_COUNT = 4;

// E1005 smooth-font pixel sizes. Smaller antialiased cuts lose stroke
// weight on the one-bit panel, so we stay at >= 18px for body text.
struct FontSpec {
  int px;
  const GFXfont* fallback;
};

// PERFORMANCE + TYPOGRAPHY: Body text uses a single SD smooth font size
// (18px preferred) so each screen render pays one loadFont() from SD.
// Hierarchy is restored via the hero numerals, which are drawn with the
// flash-resident FreeSansBold24pt7b (~48px bold) GFX font while no smooth
// font is loaded — zero SD I/O. Hero strings are digits/ASCII only, so
// the GFX font's Latin-1 limits never affect Norwegian body text.
inline FontSpec fontSpec(FontSize size) {
  (void)size;  // single body size policy
  return {18, &FreeSansBold9pt7b};
}

// Hero helpers: must be called while SmoothFont is UNLOADED, otherwise
// TFT_eSPI routes drawString/textWidth through the loaded .vlw font.
inline void selectHeroFont(TFT_eSPI& epaper) {
  epaper.setTextDatum(TL_DATUM);
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextColor(INK);
}

// "W31" -> "31": the W prefix makes nine 18px labels collide in ~50px
// chart slots; the bare week number centers cleanly.
inline String historyLabel(const String& uke) {
  if (uke.length() > 1 && (uke[0] == 'W' || uke[0] == 'w')) {
    return uke.substring(1);
  }
  return uke;
}


class SmoothFont {
 public:
  explicit SmoothFont(TFT_eSPI& display) : display_(display) {}

  // Load the .vlw from SD. Since all sizes map to 18px (fontSpec),
  // this is called once per screen render, not per text element.
  // Returns false when SD is not ready or the file is missing.
  bool load(FontSize size) {
    (void)size;  // single size policy: prefer 24px, fall back to 16px
    static int availablePx = -1;
    if (availablePx == 0) return false;
    if (availablePx < 0) {
      availablePx = 0;
      for (const char* name : {"sans_bold_18", "sans_bold_24", "sans_bold_16"}) {
        const String path = String("/fonts/") + name + ".vlw";
        if (SD.exists(path)) {
          availablePx = atoi(name + strlen(name) - 2);
          break;
        }
      }
    }
    if (availablePx == 0) {
      LOG.println("[font] no sans_bold vlw on SD");
      return false;
    }
    if (currentPx_ == availablePx) return true;
    display_.setFreeFont(nullptr);
    const String path = String("/fonts/sans_bold_") + String(availablePx);
    display_.loadFont(path, SD);
    currentPx_ = availablePx;
    LOG.printf("[font] %dpx loaded\n", currentPx_);
    return true;
  }

  void selectGfxFallback(FontSize size) {
    (void)size;
    display_.setFreeFont(nullptr);
    display_.setFreeFont(&FreeSansBold9pt7b);  // 18px GFX fallback
    currentPx_ = 0;
  }

  void unload() {
    if (currentPx_ == 0) return;
    display_.unloadFont();
    currentPx_ = 0;
  }

  bool loaded() const { return currentPx_ != 0; }
  int px() const { return currentPx_; }

  TFT_eSPI& display() { return display_; }

 private:
  TFT_eSPI& display_;
  int currentPx_ = 0;
};

// Draw `text` with a loaded smooth font using an alpha threshold so the
// glyph renders as solid black on the one-bit panel. Anchored at the
// top-left baseline of the glyph box (caller positions via textWidth).
// Returns false if the font isn't actually loaded (caller should fall
// back to drawString with a GFX font).
inline bool drawSmoothMonochromeColored(TFT_eSPI& epaper, const String& text,
                                        int left, int top, uint16_t color) {
  if (!epaper.fontLoaded || !epaper.fs_font || !epaper.fontFile) {
    return false;
  }
  constexpr uint8_t kSolidAlphaThreshold = 64;
  uint8_t row[256];
  int cursorX = left;
  const int cursorY = top;
  uint16_t offset = 0;
  const uint16_t length = static_cast<uint16_t>(text.length());
  auto* utf8 = reinterpret_cast<uint8_t*>(const_cast<char*>(text.c_str()));
  while (offset < length) {
    const uint16_t code = epaper.decodeUTF8(utf8, &offset, length - offset);
    if (code == 0x20) {
      cursorX += epaper.gFont.spaceWidth;
      continue;
    }
    uint16_t glyph = 0;
    if (!epaper.getUnicodeIndex(code, &glyph)) {
      cursorX += epaper.gFont.spaceWidth;
      continue;
    }
    const uint8_t width = epaper.gWidth[glyph];
    const uint8_t height = epaper.gHeight[glyph];
    const int glyphLeft = cursorX + epaper.gdX[glyph];
    const int glyphTop = cursorY + epaper.gFont.maxAscent - epaper.gdY[glyph];
    if (!epaper.fontFile.seek(epaper.gBitmap[glyph], fs::SeekSet)) {
      return false;
    }
    for (uint8_t y = 0; y < height; ++y) {
      if (epaper.fontFile.read(row, width) != width) return false;
      for (uint8_t x = 0; x < width; ++x) {
        if (row[x] >= kSolidAlphaThreshold) {
          epaper.drawPixel(glyphLeft + x, glyphTop + y, color);
        }
      }
    }
    cursorX += epaper.gxAdvance[glyph];
  }
  return true;
}

inline bool drawSmoothMonochrome(TFT_eSPI& epaper, const String& text,
                                 int left, int top) {
  return drawSmoothMonochromeColored(epaper, text, left, top, INK);
}

// Measure string width with the currently active font (smooth or GFX).
inline int textWidth(TFT_eSPI& epaper, SmoothFont& font, const String& text) {
  if (font.loaded()) return epaper.textWidth(text, 1);
  return epaper.textWidth(text);
}

// Measure string width for a specific size, loading it temporarily when the
// smooth font path is active (GFX textWidth does not depend on load()).
inline int textWidthFor(TFT_eSPI& epaper, SmoothFont& font, const String& text,
                       FontSize size) {
  (void)size;
  if (font.loaded()) return epaper.textWidth(text, 1);
  // GFX fallback: select the font first so textWidth measures the right face.
  font.selectGfxFallback(size);
  return epaper.textWidth(text);
}

// Draw a left-aligned string. Uses the smooth font when loaded, else
// selects a GFX bitmap fallback font and uses drawString.
inline void drawText(TFT_eSPI& epaper, SmoothFont& font, const String& text,
                     int left, int top, FontSize size) {
  epaper.setTextColor(INK);
  if (font.loaded()) {
    if (drawSmoothMonochrome(epaper, text, left, top)) return;
  }
  // No smooth font for this size: ensure a GFX fallback is active.
  font.selectGfxFallback(size);
  epaper.drawString(text, left, top);
}

// Same as drawText but in a caller-chosen ink color (e.g. INK_FAINT for
// the "Alternativ økt" label of distance-less activities).
inline void drawTextColored(TFT_eSPI& epaper, SmoothFont& font,
                            const String& text, int left, int top,
                            FontSize size, uint16_t color) {
  epaper.setTextColor(color);
  if (font.loaded()) {
    if (drawSmoothMonochromeColored(epaper, text, left, top, color)) return;
  }
  font.selectGfxFallback(size);
  epaper.drawString(text, left, top);
}

// Whether a run entry represents a distance-less activity (strength,
// stretch, etc.) that should render as "Alternativ økt" rather than
// a km/pace/elevation detail row.
inline bool isAlternativOkt(const dashboard::RunEntry& r) {
  return r.km <= 0.0f && r.pace == "--:--";
}

inline int textHeight(TFT_eSPI& epaper, SmoothFont& font) {
  if (font.loaded()) return epaper.gFont.yAdvance;
  return epaper.fontHeight(1);
}

inline void clearPanel(TFT_eSPI& epaper) {
  epaper.fillRect(0, 0, config::PANEL_WIDTH, config::PANEL_HEIGHT, PAPER);
}

inline void drawRule(TFT_eSPI& epaper, int y) {
  epaper.drawFastHLine(MARGIN, y, config::PANEL_WIDTH - 2 * MARGIN, INK);
}

inline void drawHairline(TFT_eSPI& epaper, int y) {
  epaper.drawFastHLine(MARGIN, y, config::PANEL_WIDTH - 2 * MARGIN, INK_FAINT);
}

inline void drawHeader(TFT_eSPI& epaper, SmoothFont& font,
                       const String& title, const String& oppdatert) {
  int y = MARGIN;
  font.load(FontSize::Small);
  drawText(epaper, font, title, MARGIN, y, FontSize::Small);
  const String updated = String("oppdatert: ") + oppdatert;
  const int w = textWidthFor(epaper, font, updated, FontSize::Tiny);
  drawText(epaper, font, updated,
           config::PANEL_WIDTH - MARGIN - w, y, FontSize::Tiny);
  y += textHeight(epaper, font) + LINE_GAP;
  drawRule(epaper, y);
}

// String formatting helpers (kept inline + simple to avoid pulling printf
// variants for float rendering on the ESP32).
inline String kmString(float km) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(km));
  return String(buf);
}

inline String pctString(int pct) {
  return String(pct) + "%";
}

inline String elevString(int m) {
  return String(m) + "m";
}

// Right-aligned helper: draws `text` flush against the right margin.
inline void drawRight(TFT_eSPI& epaper, SmoothFont& font,
                      const String& text, int y, FontSize size) {
  font.load(size);
  const int w = textWidth(epaper, font, text);
  drawText(epaper, font, text, config::PANEL_WIDTH - MARGIN - w, y, size);
}

// Draw a hero (digits/ASCII-only) string with the 48px bold GFX font.
// Requires font to be unloaded so TFT_eSPI does not hijack drawString with
// the smooth font. Returns the drawn width. drawString y is the glyph
// top-left for the GFX path with TL_DATUM. A negative top skips drawing
// and only measures.
inline int drawHero(TFT_eSPI& epaper, SmoothFont& font, const String& text,
                    int left, int top) {
  font.unload();
  selectHeroFont(epaper);
  const int w = epaper.textWidth(text);
  if (top >= 0) epaper.drawString(text, left, top);
  return w;
}

inline int heroTextWidth(TFT_eSPI& epaper, SmoothFont& font,
                         const String& text) {
  return drawHero(epaper, font, text, 0, -1);
}

// Draw hero text right-aligned against x, at vertical `top`.
inline int drawHeroRight(TFT_eSPI& epaper, SmoothFont& font,
                         const String& text, int right, int top) {
  const int w = heroTextWidth(epaper, font, text);
  return drawHero(epaper, font, text, right - w, top);
}

// Shared weekly-km bar chart for the Uke and Ar screens. `y` is the top of
// the chart area (below the section heading); labels sit below the bars.
template <typename EPaper>
inline void drawHistoryChart(EPaper& epaper, SmoothFont& font,
                             const dashboard::DashboardData& data, int y) {
  if (!data.historikk.empty()) {
    float maxKm = 0.0f;
    for (const dashboard::HistoryEntry& h : data.historikk) {
      if (h.km > maxKm) maxKm = h.km;
    }
    if (maxKm <= 0.0f) maxKm = 1.0f;
    const int chartTop = y;
    const int chartBottom = config::PANEL_HEIGHT - MARGIN - 20;
    const int chartH = chartBottom - chartTop;
    if (chartH > 20) {
      const int n = static_cast<int>(data.historikk.size());
      const int slotW = (config::PANEL_WIDTH - 2 * MARGIN) / n;
      const int barGap = 4;
      font.load(FontSize::Tiny);
      for (int i = 0; i < n; ++i) {
        const dashboard::HistoryEntry& h = data.historikk[i];
        const int barH = static_cast<int>(chartH * (h.km / maxKm));
        const int bx = MARGIN + i * slotW + barGap;
        const int bw = slotW - 2 * barGap;
        if (h.naa) {
          epaper.fillRect(bx, chartBottom - barH, bw, barH, INK);
        } else {
          epaper.drawRect(bx, chartBottom - barH, bw, barH, INK);
        }
        const String label = historyLabel(h.uke);
        const int lw = textWidth(epaper, font, label);
        const int lx = MARGIN + i * slotW + (slotW - lw) / 2;
        drawText(epaper, font, label, lx, chartBottom + 4, FontSize::Tiny);
      }
    }
  }
}

// ── Screen 1: Uke ───────────────────────────────────────────────────
// km, mål%, type-fordeling, høydemeter, total tid, mot forrige uke.
template <typename EPaper>
inline void renderUke(EPaper& epaper, SmoothFont& font,
                      const dashboard::DashboardData& data) {
  clearPanel(epaper);
  drawHeader(epaper, font, data.uke.merkelapp, data.oppdatert);
  int y = MARGIN + textHeight(epaper, font) + LINE_GAP * 3;

  // Hero row: total km (48px bold GFX) + goal percentage right-aligned.
  // Both are digits/ASCII only, so no Norwegian glyph is needed here.
  // Both heroes draw while the smooth font is unloaded (single SD load
  // happens afterwards, when the body text needs it again).
  {
    const String km = kmString(data.uke.total_km);
    const String pct = pctString(data.uke.maal_pct);
    const int kmW = drawHero(epaper, font, km, MARGIN, y);
    drawHeroRight(epaper, font, pct, config::PANEL_WIDTH - MARGIN, y);

    font.load(FontSize::Small);
    drawText(epaper, font, "km", MARGIN + kmW + 10, y + 32, FontSize::Small);
    const String goal = String("av ") + data.maal_km + " km";
    const int gw = textWidth(epaper, font, goal);
    drawText(epaper, font, goal,
             config::PANEL_WIDTH - MARGIN - gw, y + 56, FontSize::Small);
  }
  y += 68 + LINE_GAP * 2;
  drawHairline(epaper, y);
  y += LINE_GAP * 2;

  // Stats row: total tid + elevation + mot forrige.
  font.load(FontSize::Small);
  drawText(epaper, font, "Tid", MARGIN, y, FontSize::Small);
  drawRight(epaper, font, data.uke.total_tid, y, FontSize::Small);
  y += textHeight(epaper, font) + LINE_GAP;
  drawText(epaper, font, "Høyde", MARGIN, y, FontSize::Small);
  drawRight(epaper, font, elevString(data.uke.elevation_m), y, FontSize::Small);
  y += textHeight(epaper, font) + LINE_GAP;
  drawText(epaper, font, "vs forrige", MARGIN, y, FontSize::Small);
  drawRight(epaper, font, data.uke.mot_forrige_km + " km", y, FontSize::Small);
  y += textHeight(epaper, font) + LINE_GAP;
  drawHairline(epaper, y);
  y += LINE_GAP * 2;

  // Type breakdown.
  font.load(FontSize::Small);
  drawText(epaper, font, "Typer", MARGIN, y, FontSize::Small);
  y += textHeight(epaper, font) + LINE_GAP;
  for (const dashboard::TypeEntry& t : data.typer) {
    const String left = String(t.type) + " (" + t.antall + ")";
    const String right = kmString(t.km) + "km";
    drawText(epaper, font, left, MARGIN, y, FontSize::Small);
    const int rw = textWidth(epaper, font, right);
    drawText(epaper, font, right, config::PANEL_WIDTH - MARGIN - rw, y,
             FontSize::Small);
    y += textHeight(epaper, font) + LINE_GAP;
  }
  y += LINE_GAP;
  drawHairline(epaper, y);
  y += LINE_GAP * 2;

  // History bar chart (weekly km).
  font.load(FontSize::Tiny);
  drawText(epaper, font, "Historikk", MARGIN, y, FontSize::Tiny);
  y += textHeight(epaper, font) + LINE_GAP;
  drawHistoryChart(epaper, font, data, y);
  font.unload();
}

// ── Screen 2: År ────────────────────────────────────────────────────
// Total km i år + ukeshistorikk som søyler.
template <typename EPaper>
inline void renderAar(EPaper& epaper, SmoothFont& font,
                      const dashboard::DashboardData& data) {
  clearPanel(epaper);
  drawHeader(epaper, font, String("År: ") + "2026", data.oppdatert);
  int y = MARGIN + textHeight(epaper, font) + LINE_GAP * 3;

  // Big year total (48px bold GFX hero; digits only).
  {
    const String km = kmString(data.aar.total_km);
    const int kmW = drawHero(epaper, font, km, MARGIN, y);
    font.load(FontSize::Small);
    drawText(epaper, font, "km i år", MARGIN + kmW + 10, y + 30,
             FontSize::Small);
  }
  y += 62 + LINE_GAP * 2;
  drawHairline(epaper, y);
  y += LINE_GAP * 2;

  // History bar chart (weekly km) — taller on this screen since there
  // is more vertical space below the header.
  font.load(FontSize::Small);
  drawText(epaper, font, "Ukeshistorikk", MARGIN, y, FontSize::Small);
  y += textHeight(epaper, font) + LINE_GAP;
  drawHistoryChart(epaper, font, data, y);
  font.unload();
}

// ── Screen 3: Siste aktivitet ──────────────────────────────────────
// Detaljert visning av de siste løpene med høydemeter.
template <typename EPaper>
inline void renderSiste(EPaper& epaper, SmoothFont& font,
                        const dashboard::DashboardData& data) {
  clearPanel(epaper);
  drawHeader(epaper, font, "Siste løp", data.oppdatert);
  int y = MARGIN + textHeight(epaper, font) + LINE_GAP * 3;

  font.load(FontSize::Small);
  const size_t runsToShow =
      data.siste_lop.size() > 9 ? 9 : data.siste_lop.size();
  for (size_t i = 0; i < runsToShow; ++i) {
    const dashboard::RunEntry& r = data.siste_lop[i];

    // Date + type on the left.
    const String left = r.dato + "  " + r.type;
    drawText(epaper, font, left, MARGIN, y, FontSize::Small);
    y += textHeight(epaper, font) + LINE_GAP;

    if (isAlternativOkt(r)) {
      // Distance-less activity (strength, stretch, etc.): no km/pace/
      // elevation block — render a faint "Alternativ økt" label instead
      // (per design PR #33).
      drawTextColored(epaper, font, "Alternativ økt", MARGIN, y,
                     FontSize::Small, INK_FAINT);
      y += textHeight(epaper, font) + LINE_GAP * 2;
    } else {
      // Detail row: km + pace + elevation.
      const String detail = kmString(r.km) + "km  " + r.pace + "  " +
                            elevString(r.elevation_m);
      drawText(epaper, font, detail, MARGIN, y, FontSize::Small);
      y += textHeight(epaper, font) + LINE_GAP * 2;
    }

    // Separator between runs (except after last).
    if (i + 1 < runsToShow) {
      drawHairline(epaper, y);
      y += LINE_GAP * 2;
    }
  }

  if (data.siste_lop.empty()) {
    font.load(FontSize::Medium);
    const String msg = "Ingen løp ennå";
    const int w = textWidth(epaper, font, msg);
    drawText(epaper, font, msg,
             (config::PANEL_WIDTH - w) / 2, y + 40, FontSize::Medium);
  }
  font.unload();
}

// ── Screen 4: Journal ──────────────────────────────────────────────
// Siste løp med notater.
template <typename EPaper>
inline void renderJournal(EPaper& epaper, SmoothFont& font,
                         const dashboard::DashboardData& data) {
  clearPanel(epaper);
  drawHeader(epaper, font, "Journal", data.oppdatert);
  int y = MARGIN + textHeight(epaper, font) + LINE_GAP * 3;

  font.load(FontSize::Small);
  if (data.journal.empty()) {
    const String msg = "Ingen notater";
    const int w = textWidth(epaper, font, msg);
    drawText(epaper, font, msg,
             (config::PANEL_WIDTH - w) / 2, y + 40, FontSize::Small);
    font.unload();
    return;
  }

  const size_t toShow =
      data.journal.size() > 9 ? 9 : data.journal.size();
  for (size_t i = 0; i < toShow; ++i) {
    const dashboard::JournalEntry& j = data.journal[i];

    // Date + type header.
    const String head = j.dato + "  " + j.type;
    drawText(epaper, font, head, MARGIN, y, FontSize::Small);
    y += textHeight(epaper, font) + LINE_GAP;

    // Note (may wrap — simple word-wrap to panel width).
    const int maxW = config::PANEL_WIDTH - 2 * MARGIN;
    String line;
    line.reserve(j.note.length());
    for (int ci = 0; ci < static_cast<int>(j.note.length()); ++ci) {
      line += j.note[ci];
      if (j.note[ci] == ' ' || ci == static_cast<int>(j.note.length()) - 1) {
        const int lw = textWidth(epaper, font, line);
        if (lw > maxW) {
          // Trim back to last space.
          const int lastSpace = line.lastIndexOf(' ');
          if (lastSpace > 0) {
            const String toDraw = line.substring(0, lastSpace);
            drawText(epaper, font, toDraw, MARGIN, y, FontSize::Small);
            y += textHeight(epaper, font) + LINE_GAP;
            line = line.substring(lastSpace + 1);
          } else {
            drawText(epaper, font, line, MARGIN, y, FontSize::Small);
            y += textHeight(epaper, font) + LINE_GAP;
            line = "";
          }
        }
      }
    }
    if (line.length() > 0) {
      drawText(epaper, font, line, MARGIN, y, FontSize::Small);
      y += textHeight(epaper, font) + LINE_GAP;
    }

    y += LINE_GAP;
    if (i + 1 < toShow) {
      drawHairline(epaper, y);
      y += LINE_GAP * 2;
    }
  }
  font.unload();
}

// Dispatch: render the requested screen.
template <typename EPaper>
inline void renderScreen(EPaper& epaper, SmoothFont& font,
                         Screen screen,
                         const dashboard::DashboardData& data) {
  switch (screen) {
    case Screen::Uke:    renderUke(epaper, font, data); break;
    case Screen::Aar:    renderAar(epaper, font, data); break;
    case Screen::Siste:  renderSiste(epaper, font, data); break;
    case Screen::Journal: renderJournal(epaper, font, data); break;
  }
}

// Minimal status screen for WiFi/fetch/parse failures.
template <typename EPaper>
inline void renderStatus(EPaper& epaper, SmoothFont& font,
                         const String& title, const String& detail) {
  clearPanel(epaper);
  int y = MARGIN + 60;
  font.load(FontSize::Large);
  const int tw = textWidth(epaper, font, title);
  drawText(epaper, font, title, (config::PANEL_WIDTH - tw) / 2, y,
           FontSize::Large);
  y += textHeight(epaper, font) + LINE_GAP * 4;
  font.load(FontSize::Small);
  const int maxW = config::PANEL_WIDTH - 2 * MARGIN;
  String line;
  line.reserve(detail.length());
  for (int ci = 0; ci < static_cast<int>(detail.length()); ++ci) {
    line += detail[ci];
    if (detail[ci] == ' ' || ci == static_cast<int>(detail.length()) - 1) {
      if (textWidth(epaper, font, line) > maxW) {
        const int lastSpace = line.lastIndexOf(' ');
        const String toDraw =
            lastSpace > 0 ? line.substring(0, lastSpace) : line;
        const int dw = textWidth(epaper, font, toDraw);
        drawText(epaper, font, toDraw, (config::PANEL_WIDTH - dw) / 2, y,
                 FontSize::Small);
        y += textHeight(epaper, font) + LINE_GAP;
        line = lastSpace > 0 ? line.substring(lastSpace + 1) : "";
      }
    }
  }
  if (line.length() > 0) {
    const int dw = textWidth(epaper, font, line);
    drawText(epaper, font, line, (config::PANEL_WIDTH - dw) / 2, y,
             FontSize::Small);
  }
  font.unload();
}

}  // namespace dashboard_render
