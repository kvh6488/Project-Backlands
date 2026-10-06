#pragma once

// ============================================================================
// HeadlessHarness — records checkpoints of a scripted run to disk
// ============================================================================
// The CaptureSink (core/capture.hpp) the headless run plugs into Application.
// It knows which ticks are checkpoints by asking the ScriptedInput, and on
// those ticks - and only those - it turns the two render hooks and the
// telemetry snapshot into files:
//
//   <out>/<checkpoint>/scene.png       canvas texels, 1:1, pre-shader/pre-UI
//   <out>/<checkpoint>/frame.png       the back buffer, window-sized
//   <out>/<checkpoint>/telemetry.json  the Telemetry value, plus tick/seed
//   <out>/run.json                     what ran: seed, ticks, checkpoint list
//
// Everything written is a pure function of (scenario, seed, build): PNGs go
// through stb_image_write with no metadata, floats print round-trip exact,
// and the game side is deterministic by Phase 1. Two runs of one scenario
// must therefore be byte-identical - that is the harness's own test.
//
// Nothing here touches the game; it only reads what it is handed.
// ============================================================================

#include "core/capture.hpp"
#include "dev/debug_log.hpp"
#include "dev/json_writer.hpp"
#include "dev/scripted_input.hpp"
#include "raylib.h"
#include "rlgl.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class HeadlessHarness : public CaptureSink {
public:
  HeadlessHarness(const ScriptedInput &input, std::string outDir,
                  unsigned int seed, std::string scenarioPath)
      : m_input(input), m_outDir(std::move(outDir)), m_seed(seed),
        m_scenarioPath(std::move(scenarioPath)) {}

  void beginTick(int tick) override {
    m_checkpoint = m_input.checkpointAt(tick);
    if (!m_checkpoint)
      return;
    m_dir = std::filesystem::path(m_outDir) / *m_checkpoint;
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    if (ec) {
      debuglog::log("HEADLESS", "cannot create %s: %s", m_dir.string().c_str(),
                    ec.message().c_str());
      m_checkpoint = nullptr;
    }
  }

  void onSceneReady(const RenderTexture2D &canvas) override {
    if (!m_checkpoint)
      return;
    // A render texture's rows are stored bottom-up (GL convention), which
    // is the same reason the blit uses a negative source height.
    Image img = LoadImageFromTexture(canvas.texture);
    ImageFlipVertical(&img);
    save(img, "scene.png");
    UnloadImage(img);
  }

  void onFrameReady() override {
    if (!m_checkpoint)
      return;
    // rlgl batches draw calls and only flushes them at EndDrawing (or when
    // the batch fills / the texture changes). glReadPixels does not flush,
    // so without this the UI drawn last is missing from the capture.
    rlDrawRenderBatchActive();
    Image img = LoadImageFromScreen();
    // The plan flagged reading a hidden window's back buffer as the one
    // unverifiable step; an empty image here is the fallback signal.
    m_frameReadable = img.data != nullptr && img.width > 0;
    if (m_frameReadable)
      save(img, "frame.png");
    UnloadImage(img);
  }

  void endTick(int tick, const Telemetry &t) override {
    if (!m_checkpoint)
      return;
    JsonWriter j;
    j.beginObject();
    j.field("tick", tick);
    j.field("checkpoint", *m_checkpoint);
    j.field("seed", m_seed);
    j.field("world", t.world);

    j.key("clock");
    j.beginObject();
    j.field("day", t.day);
    j.field("season", t.season);
    j.field("dayOfSeason", t.dayOfSeason);
    j.field("timeOfDay", t.timeOfDay);
    j.field("yearDay", t.yearDay);
    j.endObject();

    j.key("player");
    j.beginObject();
    j.key("worldPos");
    vec2(j, t.playerWorldPos);
    j.key("cell");
    j.beginArray(true);
    j.value(t.playerCellX);
    j.value(t.playerCellY);
    j.endArray();
    j.field("areaState", t.areaState);
    j.field("facing", t.facing);
    j.field("mushroomEffect", t.mushroomEffect);
    j.field("passingOut", t.passingOut);
    j.endObject();

    j.key("camera");
    j.beginObject();
    j.key("target");
    vec2(j, t.cameraTarget);
    j.field("zoom", t.cameraZoom);
    j.key("rect");
    j.beginArray(true);
    j.value(t.cameraRect.x);
    j.value(t.cameraRect.y);
    j.value(t.cameraRect.width);
    j.value(t.cameraRect.height);
    j.endArray();
    j.key("canvas");
    j.beginArray(true);
    j.value(t.canvasW);
    j.value(t.canvasH);
    j.endArray();
    j.field("blitScale", t.blitScale);
    j.endObject();

    j.key("ui");
    j.beginObject();
    j.field("inventoryOpen", t.inventoryOpen);
    j.field("cupboardOpen", t.cupboardOpen);
    j.field("fullscreenMapOpen", t.fullscreenMapOpen);
    j.endObject();

    j.key("inventory");
    j.beginArray();
    for (const Telemetry::Slot &s : t.inventory) {
      j.beginObject();
      j.field("slot", s.slot);
      j.field("type", s.type);
      j.field("count", s.count);
      j.endObject();
    }
    j.endArray();

    j.key("visibleItems");
    j.beginArray();
    for (const Telemetry::WorldItem &w : t.visibleItems) {
      j.beginObject();
      j.key("cell");
      j.beginArray(true);
      j.value(w.x);
      j.value(w.y);
      j.endArray();
      j.field("type", w.type);
      j.endObject();
    }
    j.endArray();

    if (t.world == "overworld") {
      j.key("overworld");
      j.beginObject();
      j.field("size", t.worldSize);
      j.key("cell");
      j.beginArray(true);
      j.value(t.wrappedCellX);
      j.value(t.wrappedCellY);
      j.endArray();
      j.key("chunk");
      j.beginArray(true);
      j.value(t.chunkX);
      j.value(t.chunkY);
      j.endArray();
      j.field("height", t.height);
      j.field("biome", t.biome);
      j.field("celsius", t.celsius);
      j.field("snow", t.snow);
      j.key("spawn");
      j.beginArray(true);
      j.value(t.spawnX);
      j.value(t.spawnY);
      j.endArray();
      j.field("cachedChunks", t.cachedChunks);
      j.key("nearbyProps");
      j.beginArray();
      for (const Telemetry::WorldItem &p : t.nearbyProps) {
        j.beginObject();
        j.key("cell");
        j.beginArray(true);
        j.value(p.x);
        j.value(p.y);
        j.endArray();
        j.field("type", p.type);
        j.endObject();
      }
      j.endArray();
      j.endObject();
    } else {
      j.key("maze");
      j.beginObject();
      j.field("width", t.mazeWidth);
      j.field("height", t.mazeHeight);
      j.field("nonWallCount", t.nonWallCount);
      j.field("corridorCount", t.corridorCount);
      j.field("regenCount", t.regenCount);
      j.field("radiationLevel", t.radiationLevel);
      j.field("radiationDarkness", t.radiationDarkness);
      j.endObject();
    }

    j.field("frameCaptured", m_frameReadable);
    j.endObject();

    write("telemetry.json", j.str());
    m_written.push_back({*m_checkpoint, tick});
    debuglog::log("HEADLESS", "checkpoint %-16s tick %5d  cell (%d,%d) %s",
                  m_checkpoint->c_str(), tick, t.playerCellX, t.playerCellY,
                  t.areaState.c_str());
  }

  // Call once after run() returns. ticksRun is how many ticks actually
  // happened (a --ticks cap can cut the scenario short).
  void finish(int ticksRun) {
    std::error_code ec;
    std::filesystem::create_directories(m_outDir, ec);
    JsonWriter j;
    j.beginObject();
    j.field("scenario", m_scenarioPath);
    j.field("seed", m_seed);
    j.field("ticksRun", ticksRun);
    j.field("ticksScripted", m_input.tickCount());
    j.field("frameCaptured", m_frameReadable);
    j.key("checkpoints");
    j.beginArray();
    for (const Written &w : m_written) {
      j.beginObject();
      j.field("name", w.name);
      j.field("tick", w.tick);
      j.endObject();
    }
    j.endArray();
    j.endObject();
    m_dir = m_outDir;
    write("run.json", j.str());
    debuglog::log("HEADLESS", "%d ticks, %d/%d checkpoints -> %s", ticksRun,
                  (int)m_written.size(), (int)m_input.checkpointNames().size(),
                  m_outDir.c_str());
    if (!m_frameReadable && !m_written.empty()) {
      debuglog::log("HEADLESS",
                    "frame.png unavailable (hidden back buffer read empty); "
                    "scene.png is authoritative");
    }
  }

  int checkpointsWritten() const { return (int)m_written.size(); }

private:
  struct Written {
    std::string name;
    int tick;
  };

  const ScriptedInput &m_input;
  std::string m_outDir;
  unsigned int m_seed;
  std::string m_scenarioPath;

  const std::string *m_checkpoint = nullptr; // this tick's, or null
  std::filesystem::path m_dir;               // this checkpoint's directory
  bool m_frameReadable = true;
  std::vector<Written> m_written;

  static void vec2(JsonWriter &j, Vector2 v) {
    j.beginArray(true);
    j.value(v.x);
    j.value(v.y);
    j.endArray();
  }

  void save(const Image &img, const char *file) {
    std::string path = (m_dir / file).string();
    if (!ExportImage(img, path.c_str()))
      debuglog::log("HEADLESS", "failed to write %s", path.c_str());
  }

  void write(const char *file, const std::string &text) {
    std::string path = (m_dir / file).string();
    std::ofstream out(path, std::ios::binary);
    if (!out)
      debuglog::log("HEADLESS", "failed to write %s", path.c_str());
    out << text;
  }
};
