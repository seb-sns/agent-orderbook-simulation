#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

// Open-addressing hash map with Robin Hood probing and backward-shift
// deletion. Each occupied entry stores its probe distance from its home slot;
// the invariant "an entry's stored distance equals its actual offset from
// home" is what makes the early-exit in find/erase correct.
template <typename T, typename U, typename Hasher = std::hash<T>>
class FlatHashMap {
private:
  enum class State { Empty, Occupied };
  struct Entry {
    T key;
    U value;
    State state{State::Empty};
    std::uint16_t distance{0};
  };
  std::vector<Entry> data;
  std::size_t mask;
  std::size_t size_{0};
  Hasher hasher;

public:
  FlatHashMap(std::size_t capacity);
  bool insert(const T &key, const U &value);
  U *find(const T key);
  bool erase(const T key);
  std::size_t size() const { return size_; }
};

template <typename T, typename U, typename Hasher>
FlatHashMap<T, U, Hasher>::FlatHashMap(std::size_t capacity)
    : data(capacity), mask(capacity - 1) {
  assert((capacity & (capacity - 1)) == 0); // assert capacity is power of 2
};

template <typename T, typename U, typename Hasher>
bool FlatHashMap<T, U, Hasher>::insert(const T &key, const U &value) {
  if (size_ >= data.size()) {
    // Without this an insert into a full map spins forever.
    throw std::length_error("Inserting into a full FlatHashMap");
  }
  Entry newEntry{key, value, State::Occupied, 0};
  std::uint16_t distance = 0;
  std::size_t index = hasher(key) & mask;

  while (true) {

    Entry &entry = data[index];

    if (entry.state == State::Empty) {
      newEntry.distance = distance;
      data[index] = newEntry;
      ++size_;
      return true;
    }

    if (entry.key == newEntry.key) {
      entry.value = newEntry.value;
      return false;
    }

    if (distance > entry.distance) {
      // Rob the rich: the carried entry is further from home than the
      // resident, so it takes the slot and we carry the resident onward,
      // continuing from the resident's true distance.
      newEntry.distance = distance;
      std::swap(entry, newEntry);
      distance = newEntry.distance;
    }
    if (distance == std::numeric_limits<std::uint16_t>::max()) {
      throw std::length_error("FlatHashMap probe distance overflow");
    }
    ++distance;
    index = (index + 1) & mask;
  }
}

template <typename T, typename U, typename Hasher>
U *FlatHashMap<T, U, Hasher>::find(const T key) {
  std::uint16_t distance = 0;
  std::size_t index = hasher(key) & mask;

  while (true) {
    Entry &entry = data[index];

    if (entry.state == State::Empty) {
      return nullptr;
    }

    if (distance > entry.distance) {
      return nullptr; // key would have displaced this entry — not present
    }

    if (entry.key == key) {
      return &entry.value;
    }

    ++distance;
    index = (index + 1) & mask;
  }
}

template <typename T, typename U, typename Hasher>
bool FlatHashMap<T, U, Hasher>::erase(const T key) {
  std::uint16_t distance = 0;
  std::size_t index = hasher(key) & mask;

  while (true) {
    Entry &entry = data[index];
    if (entry.state == State::Empty) {
      return false;
    }
    if (distance > entry.distance) {
      return false;
    }
    if (entry.key == key) {
      break;
    }
    ++distance;
    index = (index + 1) & mask;
  }

  std::size_t next = (index + 1) & mask;
  while (data[next].state == State::Occupied && data[next].distance > 0) {
    data[index] = data[next];
    --data[index].distance;
    index = next;
    next = (next + 1) & mask;
  }

  data[index].state = State::Empty;
  data[index].distance = 0;
  --size_;
  return true;
}
