#include "core/application.hpp"
#include "dev/debug_log.hpp"
#include "dev/debug_seeds.hpp"
#include "dev/dev_mode.hpp"
#include "dev/headless_harness.hpp"
#include "dev/headless_mode.hpp"
#include "dev/scenario.hpp"
#include "dev/scripted_input.hpp"

int main(int argc, char **argv) {
  // Before anything that can log — seed resolution warns about a bad --seed.
  debuglog::enableAnsiColors();

  AppConfig cfg;
  // A seed fully determines the world, so resolving it here (rather than
  // hardcoding one in MazeState) turns "reproduce that bug" into a command
  // line flag instead of a recompile. See dev/debug_seeds.hpp.
  cfg.seed = debugseeds::resolveFromArgs(argc, argv, &cfg.seedNote);
  // Without --dev the debug overlay is unreachable, so the game plays exactly
  // as a player would meet it. See dev/dev_mode.hpp.
  cfg.devMode = devmode::enabledFromArgs(argc, argv);

  headless::Options hl = headless::parseArgs(argc, argv);
  if (!hl.error.empty()) {
    debuglog::log("HEADLESS", "%s", hl.error.c_str());
    return 2;
  }
  if (!hl.enabled) {
    Application app(cfg);
    app.run();
    return 0;
  }

  // --- Headless: scenario in, artifacts out. See dev/headless_mode.hpp ---
  scenario::Scenario sc;
  std::string error;
  if (!scenario::parseFile(hl.scenarioPath, sc, error)) {
    debuglog::log("HEADLESS", "%s: %s", hl.scenarioPath, error.c_str());
    return 2;
  }
  if (sc.seed != 0) {
    cfg.seed = sc.seed;
    cfg.seedNote = "pinned by the scenario";
  }
  cfg.headless = true;
  cfg.windowW = sc.windowW;
  cfg.windowH = sc.windowH;
  cfg.blitScale = sc.blitScale;

  // Both outlive the Application, which only borrows them.
  ScriptedInput input(sc);
  HeadlessHarness harness(input, hl.outDir, cfg.seed, hl.scenarioPath);
  cfg.input = &input;
  cfg.capture = &harness;

  debuglog::log("HEADLESS", "%s: %d ticks, %d checkpoints -> %s",
                hl.scenarioPath, input.tickCount(),
                (int)input.checkpointNames().size(), hl.outDir.c_str());

  int ticksRun = 0;
  {
    Application app(cfg);
    RunConfig run;
    run.maxTicks = hl.maxTicks;
    ticksRun = app.run(run);
  }
  harness.finish(ticksRun);
  return 0;
}
