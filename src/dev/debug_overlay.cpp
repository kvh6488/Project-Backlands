#include "dev/debug_overlay.hpp"
#include "core/palette.hpp"
#include "imgui.h"
#include "items/item.hpp"
#include "render/overworld_renderer.hpp"
#include "render/theme.hpp"
#include "rlImGui.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iterator>

namespace {

// Dim label on the left, value right-aligned on the same line. Keeps the long
// list of readouts scannable down a single column instead of ragged.
void statRow(const char *label, const char *fmt, ...) {
  char value[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(value, sizeof(value), fmt, args);
  va_end(args);

  ImGui::TextColored(ImVec4(0.62f, 0.64f, 0.70f, 1.0f), "%s", label);
  float valueWidth = ImGui::CalcTextSize(value).x;
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - valueWidth);
  ImGui::TextUnformatted(value);
}

// Full-width button, so stacked actions line up rather than sizing themselves
// to their captions.
bool wideButton(const char *label) {
  return ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));
}

// The tile under the mouse on a map image just drawn (the last ImGui item),
// with a hover tooltip. True on a left click.
bool pickTile(ImVec2 mapPos, ImVec2 mapSize, int worldW, int worldH, int &tx,
              int &ty) {
  if (!ImGui::IsItemHovered())
    return false;
  ImVec2 m = ImGui::GetMousePos();
  tx = std::clamp((int)((m.x - mapPos.x) / mapSize.x * worldW), 0, worldW - 1);
  ty = std::clamp((int)((m.y - mapPos.y) / mapSize.y * worldH), 0, worldH - 1);
  ImGui::SetTooltip("(%d, %d)  click to teleport", tx, ty);
  return ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

} // namespace

DebugOverlay::DebugOverlay(bool devToolsAvailable)
    : m_available(devToolsAvailable), m_visible(devToolsAvailable) {
  m_mapTexture.id = 0;
}

DebugOverlay::~DebugOverlay() {
  if (m_mapTexture.id != 0) {
    UnloadRenderTexture(m_mapTexture);
  }
  if (m_islandTexture.id != 0) {
    UnloadTexture(m_islandTexture);
  }
}

// ----------------------------------------------------------------------------
// Theme — scoped so the panel's look does not bleed into other ImGui windows
// ----------------------------------------------------------------------------
void DebugOverlay::pushTheme() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 0.96f));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.13f, 0.13f, 0.16f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                        ImVec4(0.17f, 0.17f, 0.21f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.21f, 0.26f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                        ImVec4(0.26f, 0.28f, 0.34f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                        ImVec4(0.30f, 0.32f, 0.40f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.19f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                        ImVec4(0.21f, 0.21f, 0.25f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.23f, 0.29f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.30f, 0.32f, 0.40f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.38f, 0.41f, 0.52f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                        ImVec4(0.45f, 0.50f, 0.66f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                        ImVec4(0.56f, 0.62f, 0.80f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.56f, 0.72f, 0.95f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.30f, 1.00f));
}

void DebugOverlay::popTheme() {
  ImGui::PopStyleColor(15);
  ImGui::PopStyleVar(8);
}

// ----------------------------------------------------------------------------
// Frame entry point
// ----------------------------------------------------------------------------
void DebugOverlay::render(Player &player, Maze &maze, RenderSettings &settings,
                          float scale) {
  if (!m_visible) {
    return;
  }

  // The minimap redraws every cell of the maze, so it is cached and rebuilt
  // only when the layout changes or the maze is resized.
  if (m_mapTexture.id == 0 || m_mapTexture.texture.width != maze.getWidth() ||
      m_mapDirty) {
    if (m_mapTexture.id == 0 || m_mapTexture.texture.width != maze.getWidth()) {
      if (m_mapTexture.id != 0)
        UnloadRenderTexture(m_mapTexture);
      m_mapTexture = LoadRenderTexture(maze.getWidth(), maze.getHeight());
    }
    generateMap(maze, settings);
    m_mapDirty = false;
  }

  panel(scale, [&]() {
    drawWorldSection(maze);
    drawViewSection(settings);
    drawLightingSection(settings);
    drawGenerationSection(settings);
    drawTripSection(player);
    drawMagicBookSection(maze);
    drawMinimapSection(player, maze);
  });
}

void DebugOverlay::render(Player &player, Overworld &world, const Calendar &cal,
                          RenderSettings &settings, float scale) {
  if (!m_visible) {
    return;
  }
  // The biome and degree views follow the date; recolour every ~2.4 game
  // hours rather than every frame.
  const double yearDay = cal.yearDay();
  if (m_islandView != 1 && std::abs(yearDay - m_islandMapYearDay) >= 0.1)
    m_islandMapDirty = true;
  if (m_islandTexture.id == 0 || m_islandMapDirty) {
    generateIslandMap(world, yearDay);
    m_islandMapDirty = false;
  }
  panel(scale, [&]() {
    drawCalendarSection(cal);
    drawClimateSection(player, world, cal);
    drawIslandSection(player, world);
    drawViewSection(settings);
    drawIslandMapSection(player, world, cal);
  });
}

template <typename Body> void DebugOverlay::panel(float scale, Body body) {
  rlImGuiBegin();
  ImGui::GetIO().FontGlobalScale = scale;
  pushTheme();

  ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 620), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(280, 160),
                                      ImVec2(FLT_MAX, FLT_MAX));

  if (ImGui::Begin("Debug Engine  (F1)")) {
    int fps = GetFPS();
    ImVec4 fpsColor = fps >= 55   ? ImVec4(0.45f, 0.85f, 0.50f, 1.0f)
                      : fps >= 30 ? ImVec4(0.95f, 0.80f, 0.35f, 1.0f)
                                  : ImVec4(0.95f, 0.42f, 0.42f, 1.0f);
    ImGui::TextColored(fpsColor, "%d FPS", fps);
    ImGui::SameLine();
    ImGui::TextDisabled("|  seed %u", m_seed);
    ImGui::Separator();
    body();
  }
  ImGui::End();

  popTheme();
  rlImGuiEnd();
}

// ----------------------------------------------------------------------------
// Sections
// ----------------------------------------------------------------------------
void DebugOverlay::drawWorldSection(Maze &maze) {
  if (!ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  ImGui::Indent();

  int totalCells = maze.getWidth() * maze.getHeight();
  statRow("Dimensions", "%d x %d", maze.getWidth(), maze.getHeight());
  statRow("Rooms", "%zu", maze.getRooms().size());
  statRow("Open cells", "%.1f%%",
          ((float)maze.getNonWallCount() / totalCells) * 100.0f);
  statRow("Corridors", "%.1f%%",
          ((float)maze.getCorridorCount() / totalCells) * 100.0f);

  ImGui::Spacing();
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "Backlands.exe --dev --seed %u", m_seed);
  ImGui::TextDisabled("Reproduce this world:");
  ImGui::TextWrapped("%s", cmd);
  if (wideButton("Copy launch command")) {
    ImGui::SetClipboardText(cmd);
  }

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawViewSection(RenderSettings &settings) {
  if (!ImGui::CollapsingHeader("View")) {
    return;
  }
  ImGui::Indent();

  // Only the scales that keep art pixels whole are offered; see
  // RenderSettings::blitScale.
  static const char *const kLabels[] = {"1x  (40 tiles)", "1.5x  (27 tiles)",
                                        "2x  (20 tiles)", "3x  (13 tiles)"};
  int current = 0;
  for (int i = 0; i < RenderSettings::kBlitScaleCount; ++i) {
    if (settings.blitScale == RenderSettings::kBlitScales[i])
      current = i;
  }
  if (ImGui::Combo("Zoom", &current, kLabels,
                   RenderSettings::kBlitScaleCount)) {
    settings.blitScale = RenderSettings::kBlitScales[current];
  }
  if (wideButton(IsWindowFullscreen() ? "Exit fullscreen  (F11)"
                                      : "Enter fullscreen  (F11)")) {
    ToggleFullscreen();
  }

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawLightingSection(RenderSettings &settings) {
  if (!ImGui::CollapsingHeader("Lighting")) {
    return;
  }
  ImGui::Indent();

  ImGui::Checkbox("Flashlight torch mode", &settings.flashlightEnabled);

  // The mask texture is rebuilt only when one of these actually moves.
  ImGui::BeginDisabled(!settings.flashlightEnabled);
  if (ImGui::SliderFloat("Cone angle", &settings.lightConeAngle, 90.0f, 360.0f,
                         "%.0f deg"))
    settings.lightSettingsChanged = true;
  if (ImGui::SliderFloat("Radius", &settings.lightSizeScale, 1.0f, 6.0f,
                         "%.2f"))
    settings.lightSettingsChanged = true;
  if (ImGui::SliderFloat("Edge fade", &settings.lightFadeStrength, 0.1f, 10.0f,
                         "%.2f"))
    settings.lightSettingsChanged = true;
  ImGui::EndDisabled();

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawGenerationSection(RenderSettings &settings) {
  if (!ImGui::CollapsingHeader("Generation")) {
    return;
  }
  ImGui::Indent();

  if (ImGui::Checkbox("Highlight shifting zones",
                      &settings.showGenerationZones)) {
    m_mapDirty = true;
  }
  // Raises the same request flag the eventual in-game trigger will, so this
  // button keeps exercising the real path.
  if (wideButton("Regenerate Tic-Tac-Toe zones")) {
    m_triggerTicTacToeRegen = true;
  }

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawTripSection(Player &player) {
  if (!ImGui::CollapsingHeader("Mushroom Trip")) {
    return;
  }
  ImGui::Indent();

  // Forcing the trip is what makes book inspection meaningful: the book is a
  // hallucination and must be judged against the distorted, darkened scene it
  // appears in, not against a sober one.
  float strength = player.getMushroomEffectStrength();
  statRow("Effect strength", "%.2f", strength);
  ImGui::ProgressBar(strength, ImVec2(-FLT_MIN, 0.0f), "");

  float half =
      (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
      0.5f;
  if (ImGui::Button("Force full trip (60s)", ImVec2(half, 0.0f))) {
    m_triggerForceTrip = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("End trip", ImVec2(half, 0.0f))) {
    m_triggerEndTrip = true;
  }

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawMagicBookSection(Maze &maze) {
  if (!ImGui::CollapsingHeader("Magic Book of Maps")) {
    return;
  }
  ImGui::Indent();

  // These controls decouple the book from its acquisition ritual (mushroom ->
  // full trip -> 1-in-3 roll -> nearby table -> reach it before the trip
  // decays). Testing the renderer should not require winning that lottery.
  if (maze.isMagicBookSpawned()) {
    statRow("State", "spawned (%d, %d)", maze.getMagicBookX(),
            maze.getMagicBookY());
  } else {
    statRow("State", "not spawned");
  }
  ImGui::TextDisabled("Last attempt:");
  ImGui::TextWrapped("%s", m_magicBookStatus.c_str());

  ImGui::Spacing();
  if (wideButton("Force spawn magic book")) {
    m_triggerMagicBookSpawn = true;
  }
  ImGui::Checkbox("Pin book (ignore trip decay)", &m_pinMagicBook);
  ImGui::SliderFloat("Trip follow", &m_bookTripFollow, 0.0f, 1.0f, "%.2f");
  ImGui::SliderFloat("Glow radius", &m_bookGlowScale, 0.3f, 2.5f, "%.2f");

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawMinimapSection(Player &player, Maze &maze) {
  if (!ImGui::CollapsingHeader("Minimap", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  ImGui::Indent();

  if (ImGui::Checkbox("Show radiation and waste", &m_showRadiation)) {
    m_mapDirty = true;
  }

  float availWidth = ImGui::GetContentRegionAvail().x;
  float mapRatio = (float)maze.getHeight() / (float)maze.getWidth();
  Vector2 mapSize = {availWidth, availWidth * mapRatio};
  ImVec2 mapPos = ImGui::GetCursorScreenPos();

  rlImGuiImageRect(&m_mapTexture.texture, (int)mapSize.x, (int)mapSize.y,
                   Rectangle{0, 0, (float)m_mapTexture.texture.width,
                             -(float)m_mapTexture.texture.height});
  int tx, ty;
  if (pickTile(mapPos, ImVec2(mapSize.x, mapSize.y), maze.getWidth(),
               maze.getHeight(), tx, ty))
    requestTeleport(tx, ty);

  // Player marker. The maze wraps toroidally, so the raw grid position is
  // wrapped before it becomes a fraction of the map.
  Vector2 pPos = player.getPosition();
  int gridX = maze.toGridX(pPos.x);
  int gridY = maze.toGridY(pPos.y);
  int wrappedX = (gridX % maze.getWidth() + maze.getWidth()) % maze.getWidth();
  int wrappedY =
      (gridY % maze.getHeight() + maze.getHeight()) % maze.getHeight();

  ImVec2 marker(mapPos.x + ((float)wrappedX / maze.getWidth()) * mapSize.x,
                mapPos.y + ((float)wrappedY / maze.getHeight()) * mapSize.y);

  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRect(mapPos, ImVec2(mapPos.x + mapSize.x, mapPos.y + mapSize.y),
                IM_COL32(70, 70, 82, 255));
  draw->AddCircleFilled(marker, 3.5f, IM_COL32(255, 60, 60, 255));
  draw->AddCircle(marker, 6.0f, IM_COL32(255, 60, 60, 120));

  ImGui::TextDisabled("red you / green radiation / blue waste / purple book");
  ImGui::TextDisabled("click to teleport");

  ImGui::Unindent();
  ImGui::Spacing();
}

// ----------------------------------------------------------------------------
// Overworld sections
// ----------------------------------------------------------------------------
void DebugOverlay::drawCalendarSection(const Calendar &cal) {
  if (!ImGui::CollapsingHeader("Calendar", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  ImGui::Indent();
  static const char *kSeasonNames[] = {"Spring", "Summer", "Autumn", "Winter"};
  const float hour = cal.timeOfDay();
  statRow("Day", "%d", cal.day());
  statRow("Season", "%s, day %d of %d", kSeasonNames[(int)cal.season()],
          cal.dayOfSeason(), Calendar::kDaysPerSeason);
  statRow("Time", "%02d:%02d", (int)hour, (int)((hour - (int)hour) * 60.0f));
  statRow("Year day", "%.2f / %d", cal.yearDay(), Calendar::kDaysPerYear);

  // The whole year on one slider, summer first; dragging scrubs the clock
  // live. The slider counts days from day 1 of summer.
  ImGui::Spacing();
  ImGui::TextDisabled("Year  (summer 0 / autumn 20 / winter 40 / spring 60)");
  const float year = (float)Calendar::kDaysPerYear;
  const float start = (float)Calendar::kScrubYearStart;
  float s = std::fmod((float)cal.yearDay() - start + year, year);
  char label[48];
  snprintf(label, sizeof(label), "%s day %d", kSeasonNames[(int)cal.season()],
           cal.dayOfSeason());
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderFloat("##year", &s, 0.0f, year - 0.01f, label)) {
    m_requestYearDay = std::fmod(s + start, year);
    m_triggerSetYearDay = true;
  }

  // Each season at its peak, at noon: midsummer, every turning tree in full
  // colour (none fallen yet), midwinter, and the spring bloom.
  struct Jump {
    const char *label;
    float yearDay;
  };
  static constexpr Jump kJumps[] = {
      {"Summer", 30.5f}, {"Autumn", 54.5f}, {"Winter", 70.5f}, {"Spring", 10.5f}};
  const float quarter =
      (ImGui::GetContentRegionAvail().x - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
  for (int i = 0; i < (int)std::size(kJumps); ++i) {
    if (i > 0)
      ImGui::SameLine();
    if (ImGui::Button(kJumps[i].label, ImVec2(quarter, 0.0f))) {
      m_requestYearDay = kJumps[i].yearDay;
      m_triggerSetYearDay = true;
    }
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Clock speed");
  for (int i = 0; i < (int)std::size(kTimeScales); ++i) {
    char label[16];
    snprintf(label, sizeof(label), "x%d", kTimeScales[i]);
    if (i > 0)
      ImGui::SameLine();
    ImGui::RadioButton(label, &m_timeScale, i);
  }
  ImGui::TextDisabled("x1440: a day per ~0.6 s, a season in ~12 s");

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawClimateSection(Player &player, Overworld &world,
                                      const Calendar &cal) {
  if (!ImGui::CollapsingHeader("Climate", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  ImGui::Indent();
  const Climate &climate = world.climate();
  const int px = world.toGridX(player.getPosition().x);
  const int py = world.toGridY(player.getPosition().y);
  const float t0 = world.temperatureAt(px, py);
  statRow("Player", "%.1f C  %s", world.celsiusAt(px, py, cal),
          world.snowAt(px, py, cal) ? "snow" : world.frozenAt(px, py, cal) ? "ice" : "");
  ImGui::TextDisabled("    %s  h %.2f  t0 %.3f  mean %.1f C", biomeId(world.biomeAt(px, py)),
                      world.heightAt(px, py), t0, climate.meanCelsius(t0, cal.yearDay()));

  // Share of all land tiles under snow.
  ImGui::Spacing();
  statRow("Snow cover", "%.1f %% of land", 100.0f * climate.landSnowShare(cal.yearDay()));

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawIslandSection(Player &player, Overworld &world) {
  if (!ImGui::CollapsingHeader("Island", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  ImGui::Indent();

  const IslandMap &map = world.island().map();
  int lone = 0;
  for (int inflow : map.lakeInflow)
    lone += inflow == 0;
  int riverCells = 0;
  for (uint8_t r : map.river)
    riverCells += r;
  statRow("World", "%d x %d tiles", world.getWidth(), world.getHeight());
  statRow("Lakes", "%zu  (%d lone)", map.lakeLevel.size(), lone);
  statRow("River cells", "%d", riverCells);
  statRow("Spawn", "(%d, %d)", world.spawnX(), world.spawnY());
  statRow("Cached chunks", "%d", world.cachedChunkCount());
  statRow("Changes", "%d", world.changeCount());

  int px = world.toGridX(player.getPosition().x);
  int py = world.toGridY(player.getPosition().y);
  statRow("Player tile", "(%d, %d)", world.wrapX(px), world.wrapY(py));
  statRow("Biome", "%s", biomeId(world.biomeAt(px, py)));
  statRow("Height", "%.3f", world.heightAt(px, py));

  ImGui::Spacing();
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "Backlands.exe --dev --world overworld --seed %u",
           m_seed);
  ImGui::TextDisabled("Reproduce this island:");
  ImGui::TextWrapped("%s", cmd);
  if (wideButton("Copy launch command")) {
    ImGui::SetClipboardText(cmd);
  }
  // Both go through OverworldState's own entry points.
  if (wideButton("New seed -> regenerate")) {
    m_triggerNewIsland = true;
  }
  if (wideButton("Remove nearest prop")) {
    m_triggerRemoveProp = true;
  }

  ImGui::Unindent();
  ImGui::Spacing();
}

void DebugOverlay::drawIslandMapSection(Player &player, Overworld &world,
                                        const Calendar &cal) {
  if (!ImGui::CollapsingHeader("Island map", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }
  ImGui::Indent();

  bool changed = ImGui::RadioButton("Biomes", &m_islandView, 0);
  ImGui::SameLine();
  changed |= ImGui::RadioButton("Height", &m_islandView, 1);
  ImGui::SameLine();
  changed |= ImGui::RadioButton("Degrees", &m_islandView, 2);
  if (changed) {
    m_islandMapDirty = true;
  }

  float side = ImGui::GetContentRegionAvail().x;
  ImVec2 mapPos = ImGui::GetCursorScreenPos();
  rlImGuiImageRect(&m_islandTexture, (int)side, (int)side,
                   Rectangle{0, 0, (float)m_islandTexture.width,
                             (float)m_islandTexture.height});
  int tx = 0, ty = 0;
  if (pickTile(mapPos, ImVec2(side, side), world.getWidth(), world.getHeight(),
               tx, ty))
    requestTeleport(tx, ty);
  if (ImGui::IsItemHovered() && !m_islandSamples.empty()) {
    // The cached coarse sample under the mouse: no chunk is built.
    const int n = world.island().map().n, k = IslandConfig::kCoarse;
    const int cx = std::clamp(tx / k, 0, n - 1), cy = std::clamp(ty / k, 0, n - 1);
    const TileSample &s = m_islandSamples[cy * n + cx];
    const Climate &climate = world.climate();
    ImGui::SetTooltip("(%d, %d)  %s\n%.1f C%s\nclick to teleport", tx, ty,
                      biomeId(s.biome), climate.celsius(s.temperature, cal),
                      climate.snow(s.temperature, s.biome, cal.yearDay()) ? "  snow"
                      : climate.frozen(s.temperature, s.height, s.biome, cal.yearDay()) ? "  ice"
                                                                                       : "");
  }

  int px = world.wrapX(world.toGridX(player.getPosition().x));
  int py = world.wrapY(world.toGridY(player.getPosition().y));
  ImVec2 marker(mapPos.x + (float)px / world.getWidth() * side,
                mapPos.y + (float)py / world.getHeight() * side);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRect(mapPos, ImVec2(mapPos.x + side, mapPos.y + side),
                IM_COL32(70, 70, 82, 255));
  draw->AddCircleFilled(marker, 3.5f, IM_COL32(255, 60, 60, 255));
  draw->AddCircle(marker, 6.0f, IM_COL32(255, 60, 60, 120));

  ImGui::Unindent();
  ImGui::Spacing();
}

// Built on the CPU and uploaded once: ~150k pixels as DrawPixel calls into a
// render texture would be ~150k draw calls.
void DebugOverlay::generateIslandMap(const Overworld &world, double yearDay) {
  const Island &island = world.island();
  const Climate &climate = world.climate();
  const int n = island.map().n;
  const int k = IslandConfig::kCoarse;
  if (m_islandSamples.size() != (size_t)n * n || m_islandSamplesSeed != island.seed()) {
    m_islandSamples.resize((size_t)n * n);
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x)
        m_islandSamples[y * n + x] = island.sample(x * k + k / 2, y * k + k / 2);
    m_islandSamplesSeed = island.seed();
  }
  m_islandMapYearDay = yearDay;
  // 4 C bands, cold to hot; the blue/yellow edge is 0 C, the snow line.
  static constexpr Color kHeat[] = {pal::blue[0],   pal::blue[1],   pal::blue[4],
                                    pal::blue[7],   pal::blue[9],   pal::yellow[12],
                                    pal::yellow[10], pal::yellow[9], pal::yellow[7],
                                    pal::accent[3], pal::accent[2]};
  Image img = GenImageColor(n, n, BLANK);
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      const TileSample &s = m_islandSamples[y * n + x];
      Color c;
      if (m_islandView == 0) {
        c = climate.snow(s.temperature, s.biome, yearDay) ? theme::snow
            : climate.frozen(s.temperature, s.height, s.biome, yearDay)
                ? theme::ice
                : OverworldRenderer::biomeColour(s.biome);
      } else if (m_islandView == 2) {
        const float deg = climate.meanCelsius(s.temperature, yearDay);
        const int band = std::clamp((int)std::floor(deg / 4.0f) + 5, 0, (int)std::size(kHeat) - 1);
        c = isWater(s.biome) ? Fade(kHeat[band], 0.55f) : kHeat[band];
      } else if (s.height < 0.0f) {
        // Deep to shallow; blue[2] is the teal pine, 3, 5 and 6 the swamp blends.
        static constexpr int kDepth[] = {0, 1, 4};
        c = pal::blue[kDepth[std::clamp(2 + (int)(s.height * 8.0f), 0, 2)]];
      } else {
        c = pal::neutral[std::clamp(1 + (int)(s.height * 7.0f), 1, 7)];
      }
      ImageDrawPixel(&img, x, y, c);
    }
  }
  if (m_islandTexture.id != 0) {
    UnloadTexture(m_islandTexture);
  }
  m_islandTexture = LoadTextureFromImage(img);
  UnloadImage(img);
}

// ----------------------------------------------------------------------------
// Minimap texture — one pixel per cell
// ----------------------------------------------------------------------------
void DebugOverlay::generateMap(Maze &maze, const RenderSettings &settings) {
  BeginTextureMode(m_mapTexture);
  ClearBackground(BLANK);

  for (int y = 0; y < maze.getHeight(); ++y) {
    for (int x = 0; x < maze.getWidth(); ++x) {
      if (maze.getCell(x, y) == Maze::CELL_WALL) {
        DrawPixel(x, y, Color{100, 100, 100, 255});
      } else if (m_showRadiation && maze.getRadiationLevel(x, y) > 0) {
        DrawPixel(x, y, Color{0, 255, 0, 255});
      } else if (settings.showGenerationZones && maze.isShiftingZone(x, y)) {
        DrawPixel(x, y, Color{255, 100, 100, 255});
      } else {
        DrawPixel(x, y, Color{30, 30, 35, 255});
      }
    }
  }

  if (m_showRadiation) {
    for (int y = 0; y < maze.getHeight(); ++y) {
      for (int x = 0; x < maze.getWidth(); ++x) {
        if (maze.getItem(x, y) == ItemType::TOXIC_WASTE) {
          DrawRectangle(x - 1, y - 1, 3, 3, BLUE);
        }
      }
    }
  }

  if (maze.isMagicBookSpawned()) {
    DrawRectangle(maze.getMagicBookX() - 1, maze.getMagicBookY() - 1, 3, 3,
                  PURPLE);
  }

  EndTextureMode();
}
