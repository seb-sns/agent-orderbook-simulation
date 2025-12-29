#include <deque>

template <typename T> class FixedDeque {
private:
  std::deque<T> dq;
  std::size_t max_size;

public:
  FixedDeque(size_t size) : max_size(size) {}

  void push_back(const T &&value) {
    if (dq.size() == max_size) {
      dq.pop_front();
    }
    dq.push_back(value);
  }

  void push_front(const T &&value) {
    if (dq.size() == max_size) {
      dq.pop_back();
    }
    dq.push_front(value);
  }

  bool pop_back(T &item) {
    if (dq.empty()) {
      return false;
    }
    item = std::move(dq.back());
    dq.pop_back();
    return true;
  }

  bool pop_front(T &item) {
    if (dq.empty()) {
      return false;
    }
    item = std::move(dq.front());
    dq.pop_front();
    return true;
  }

  size_t size() const { return dq.size(); }
  bool empty() const { return dq.empty(); }
  bool full() const { return dq.size() == max_size; }

  T &operator[](size_t index) { return dq[index]; }
  const T &operator[](size_t index) const { return dq[index]; }

  auto begin() { return dq.begin(); }
  auto end() { return dq.end(); }
  auto begin() const { return dq.begin(); }
  auto end() const { return dq.end(); }
};
