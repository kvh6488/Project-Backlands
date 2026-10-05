#pragma once

#include <algorithm>
#include <vector>

// ============================================================================
// DrawQueue — the Y-sorted render queue shared by both worlds
// ============================================================================
// Top-down 2D fakes depth with one rule: whatever stands lower on the screen
// is nearer the viewer, so it draws later. Every sprite that stands on the
// ground (the player, furniture, trees, rocks, later mobs) is pushed with its
// BASE Y - the world-pixel row its feet touch - and the queue draws them in
// ascending base Y. Terrain is not in the queue: it is flat, so it is a
// separate pass underneath everything.
//
// This replaced the maze's fixed "items behind / player / items in front"
// layers, which put every piece of furniture over the player even when the
// player stood below it.
//
// SORT: counting sort, one bucket per pixel row. Base Y is bounded by the
// rows the camera can see (plus the tall-sprite margin), so the key range R
// is about one canvas height whatever the world size: O(k + R) per frame for
// k drawables, against O(k log k) for a comparison sort. It is STABLE, which
// is what makes ties deterministic - equal base Y draws in push order - and
// a scripted run byte-identical. CLRS Ch. 8.2.
//
// Who draws: a Drawable names a Drawer and two ints of payload (usually the
// cell). A renderer binds its subject (the world, the player) when it pushes,
// then draws one entry when the queue calls it back - so the queue knows
// nothing about textures, and no closure is allocated per sprite. The vectors
// are reused frame to frame, so the steady state allocates nothing.
// ============================================================================

class Drawer {
public:
  virtual ~Drawer() = default;
  virtual void drawQueued(int a, int b) const = 0;
};

class DrawQueue {
public:
  // Base Y rows this frame can see, inclusive. Anything outside is clamped
  // into the end buckets rather than dropped - it was culled upstream if it
  // needed culling.
  void begin(int minBaseY, int maxBaseY) {
    m_minY = minBaseY;
    m_range = std::max(0, maxBaseY - minBaseY) + 1;
    m_items.clear();
  }

  void push(int baseY, const Drawer &drawer, int a = 0, int b = 0) {
    m_items.push_back({baseY, &drawer, a, b});
  }

  // Sorts and draws everything pushed since begin().
  void flush() {
    m_counts.assign(m_range + 1, 0);
    for (const Item &it : m_items)
      ++m_counts[key(it) + 1];
    for (int i = 1; i <= m_range; ++i)
      m_counts[i] += m_counts[i - 1]; // m_counts[k] = first slot of bucket k
    m_sorted.resize(m_items.size());
    for (const Item &it : m_items)
      m_sorted[m_counts[key(it)]++] = it;

    for (const Item &it : m_sorted)
      it.drawer->drawQueued(it.a, it.b);
    m_items.clear();
  }

  int size() const { return (int)m_items.size(); }

private:
  struct Item {
    int baseY;
    const Drawer *drawer;
    int a, b;
  };

  int key(const Item &it) const {
    return std::clamp(it.baseY - m_minY, 0, m_range - 1);
  }

  int m_minY = 0;
  int m_range = 1;
  std::vector<Item> m_items;
  std::vector<Item> m_sorted;
  std::vector<int> m_counts;
};
