#pragma once

#include "world/island.hpp"
#include "world/world.hpp"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Things that grow or lie on a tile. Not items: a prop is scenery, generated
// rather than placed, and never goes in the bag (felling a tree for wood is a
// later phase's recipe, not a pickup).
enum class PropType : uint8_t { NONE, TREE, PINE, BUSH, ROCK, REEDS, COUNT };

inline bool propIsSolid(PropType p) {
  return p == PropType::TREE || p == PropType::PINE || p == PropType::ROCK;
}

inline const char *propId(PropType p) {
  switch (p) {
  case PropType::TREE: return "TREE";
  case PropType::PINE: return "PINE";
  case PropType::BUSH: return "BUSH";
  case PropType::ROCK: return "ROCK";
  case PropType::REEDS: return "REEDS";
  default: return "NONE";
  }
}

struct Prop {
  int x, y;      // tile, wrapped into the world
  float px, py;  // the Poisson point it grew from, in world tiles
  PropType type;
  // A per-tile hash byte. The renderer turns it into a sprite (which species,
  // which size), so the world never names a picture.
  uint8_t variant;
};

// ============================================================================
// Overworld — the island as a World: chunks, props, and what the player changed
// ============================================================================
// TWO RESOLUTIONS. The Island holds the whole-island answers (coarse grid,
// built once). Fine detail lives in 32x32-tile CHUNKS, generated on demand
// from (seed, chunk) and dropped once the camera is far away. Same inputs,
// same chunk - so memory depends on the view, not the world: ~50 chunks
// cached whether the world is 3k or 8k tiles across.
//
// THE CHANGE RECORD. Regenerating a chunk would undo whatever the player did
// to it, so changes live outside the chunks in a sparse map keyed by tile
// (the same pattern as Maze::m_itemStates) and are re-applied every time a
// chunk is built. A felled tree stays felled however often its chunk is
// evicted. Dropped items use the same idea: a sparse map, never evicted.
//
// SCATTER. Props come from Poisson-disc points (poisson.hpp), thinned by a
// per-biome density roll. Each chunk samples its own points, which on their
// own would crowd each other at chunk borders. So a chunk also samples its
// eight neighbours, and drops any point within r of a neighbour's point when
// that neighbour has priority (lower chunk index). Whichever chunk is built
// first, both sides agree on who yields, so the spacing holds across borders
// and no chunk depends on another's build order.
// ============================================================================
class Overworld final : public World {
public:
  explicit Overworld(uint32_t seed, const IslandConfig &cfg = IslandConfig{});

  static constexpr int kChunk = IslandConfig::kChunk;
  // Minimum spacing between props, in tiles. >= sqrt(2), so no two props
  // ever share a tile.
  static constexpr float kPropSpacing = 2.0f;
  // Props keep this many tiles (Chebyshev) clear around the spawn.
  static constexpr int kSpawnClearance = 2;

  const Island &island() const { return m_island; }
  int spawnX() const { return m_island.spawnX(); }
  int spawnY() const { return m_island.spawnY(); }

  // Per-tile queries, served from the chunk cache. Any int is valid; tiles
  // wrap.
  Biome biomeAt(int x, int y) const;
  float heightAt(int x, int y) const;
  uint8_t shadeAt(int x, int y) const; // TileSample::shade
  uint8_t flowAt(int x, int y) const;  // TileSample::flow
  PropType propAt(int x, int y) const;
  // The full record of the prop on a tile, or null. Valid until its chunk is
  // evicted by retainAround - read it, do not keep it.
  const Prop *findProp(int x, int y) const;
  // The props of one chunk, after the change record. cx, cy wrap. The
  // reference lives until the chunk is evicted by retainAround.
  const std::vector<Prop> &chunkProps(int cx, int cy) const;

  // Removes the prop on a tile, through the change record. False if there
  // was none.
  bool removeProp(int x, int y);
  int changeCount() const { return (int)m_removedProps.size(); }

  // Drops cached chunks more than `radius` chunks (Chebyshev, wrap-aware)
  // from the chunk holding tile (x, y). They regenerate identically on demand.
  void retainAround(int x, int y, int radius);
  int cachedChunkCount() const { return (int)m_chunks.size(); }

  // Floor division: tile -1 is in chunk -1, not chunk 0.
  static int chunkOf(int tile) {
    return tile >= 0 ? tile / kChunk : -((-tile + kChunk - 1) / kChunk);
  }

  // --- World contract ---
  // Trees, pines and rocks block; so does any placed item. Water does not -
  // there is no swimming or boat yet, and the wrap-seam scenario has to walk
  // across open sea.
  bool isSolid(int x, int y) const override;
  ItemType getItem(int x, int y) const override;
  void setItem(int x, int y, ItemType type) override;
  int getItemState(int x, int y) const override;
  bool findNearestEmptyItemCell(int startX, int startY, int maxRadius,
                                int &outX, int &outY) const override;

  Overworld *asOverworld() override { return this; }
  const Overworld *asOverworld() const override { return this; }

private:
  struct Chunk {
    std::array<Biome, kChunk * kChunk> biome;
    std::array<float, kChunk * kChunk> height;
    std::array<uint8_t, kChunk * kChunk> shade;
    std::array<uint8_t, kChunk * kChunk> flow;
    std::array<int16_t, kChunk * kChunk> propIndex; // into props, or -1
    std::vector<Prop> props;
  };

  int tileIndex(int x, int y) const { return wrapY(y) * m_width + wrapX(x); }
  int chunkKey(int cx, int cy) const;
  const Chunk &chunk(int cx, int cy) const;
  Chunk build(int cx, int cy) const;
  void applyChanges(int cx, int cy, Chunk &c) const;
  PropType rollProp(Biome b, uint8_t shade, int x, int y, uint8_t &variant) const;

  Island m_island;
  int m_chunksAcross;

  // A cache: filling it on a const query does not change what the world IS,
  // which is why it may be mutable.
  mutable std::unordered_map<int, Chunk> m_chunks;

  // The change record: tiles whose generated prop the player removed. Placed
  // props (a campfire, a shelter) will join it as a second map when they exist.
  std::unordered_set<int> m_removedProps;

  std::unordered_map<int, ItemType> m_items;
  std::unordered_map<int, int> m_itemStates;
};
