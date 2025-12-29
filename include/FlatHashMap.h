#pragma once

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <vector>

template <typename T, typename U, typename Hasher = std::hash<T>>
class FlatHashMap {
private:
  enum class State { Empty, Occupied, Deleted };
  struct Entry {
    T key;
    U value;
    State state{State::Empty};
  };
  std::vector<Entry> data;
  std::size_t mask;
  Hasher hasher;
  static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

public:
  FlatHashMap(std::size_t capacity);
  bool insert(const Entry newEntry);
  U *find(const T key);
  bool erase(const T key);
};

template <typename T, typename U, typename Hasher>
FlatHashMap<T, U, Hasher>::FlatHashMap(std::size_t capacity)
    : data(capacity), mask(capacity - 1) {
  assert((capacity & (capacity - 1)) == 0); // assert capacity is power of 2
};

template <typename T, typename U, typename Hasher>
bool FlatHashMap<T, U, Hasher>::insert(const Entry newEntry) {
  std::size_t index = hasher(newEntry.key) & mask;
  std::size_t first_deleted = npos;

  while (true) {

    Entry &entry = data[index];

    if (entry.state == State::Empty) {
      std::size_t target = (first_deleted != npos) ? first_deleted : index;
      data[target] = {newEntry.key, newEntry.value, State::Occupied};
      return true;
    }

    if (entry.state == State::Deleted && first_deleted == npos) {
      first_deleted = index;
    } else if (entry.state == State::Occupied && entry.key == newEntry.key) {
      entry.value = newEntry.value;
      return false;
    }
    index = (index + 1) & mask;
  }
}

template <typename T, typename U, typename Hasher>
U *FlatHashMap<T, U, Hasher>::find(const T key) {
  std::size_t index = hasher(key) & mask;

  while (true) {
    Entry &entry = data[index];

    if (entry.state == State::Occupied && entry.key == key) {
      return &entry.value;
    }

    if (entry.state == State::Empty) {
      return nullptr;
    }

    index = (index + 1) & mask;
  }
}

template <typename T, typename U, typename Hasher>
bool FlatHashMap<T, U, Hasher>::erase(const T key) {
  std::size_t index = hasher(key) & mask;

  while (true) {
    Entry &entry = data[index];

    if (entry.state == State::Occupied && entry.key == key) {
      entry.state = State::Deleted;
      return true;
    }

    if (entry.state == State::Empty) {
      return false;
    }

    index = (index + 1) & mask;
  }
}
