#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#include "evacuator.hpp"
#include "log.hpp"
#include "object.hpp"
#include "resource_manager.hpp"

constexpr int kNumGCThds = 3;
constexpr int kNumThds = 10;
constexpr int kNumObjs = 40960;

constexpr int kObjSize = 111;

struct Object {
  char data[kObjSize];

  void random_fill() {
    static std::random_device rd;
    static std::mt19937 mt(rd());
    static std::uniform_int_distribution<int> dist('A', 'z');

    for (uint32_t i = 0; i < kObjSize; i++) {
      data[i] = dist(mt);
    }
  }

  bool equal(Object &other) {
    return (strncmp(data, other.data, kObjSize) == 0);
  }
};

int main(int argc, char *argv[]) {
  auto *allocator = midas::LogAllocator::global_allocator();
  std::vector<std::thread> threads;

  std::atomic_int nr_errs(0);
  std::vector<std::shared_ptr<midas::ObjectPtr>> ptrs[kNumThds];
  std::vector<Object> objs[kNumThds];

  for (int tid = 0; tid < kNumThds; tid++) {
    threads.push_back(std::thread([&, tid = tid]() {
      for (int i = 0; i < kNumObjs; i++) {
        Object obj;
        obj.random_fill();
        auto objptr = std::make_shared<midas::ObjectPtr>();

        if (!allocator->alloc_to(sizeof(Object), objptr.get()) ||
            !objptr->copy_from(&obj, sizeof(Object))) {
          nr_errs++;
          continue;
        }
        ptrs[tid].push_back(objptr);
        objs[tid].push_back(obj);
      }

      for (int i = 0; i < ptrs[tid].size(); i++) {
        bool ret = false;
        auto ptr = ptrs[tid][i];
        Object stored_o;
        if (!ptr->copy_to(&stored_o, sizeof(Object)) ||
            !objs[tid][i].equal(stored_o))
          nr_errs++;
      }
    }));
  }

  for (auto &thd : threads)
    thd.join();
  threads.clear();

  midas::Evacuator evacuator(
      nullptr, midas::ResourceManager::global_manager_shared_ptr(),
      midas::LogAllocator::global_allocator_shared_ptr());
  evacuator.parallel_gc(kNumGCThds);

  bool kTestFree = false;
  for (int tid = 0; tid < kNumThds; tid++) {
    threads.push_back(std::thread([&, tid = tid]() {
      auto nr_ptrs = ptrs[tid].size();
      for (int i = 0; i < nr_ptrs; i++) {
        auto ptr = ptrs[tid][i];
        Object stored_o;
        if (!ptr || !ptr->copy_to(&stored_o, sizeof(Object)) ||
            !objs[tid][i].equal(stored_o))
          nr_errs++;
      }

      if (kTestFree) {
        for (auto ptr : ptrs[tid]) {
          if (!allocator->free(*ptr))
            nr_errs++;
        }
      }
    }));
  }
  for (auto &thd : threads)
    thd.join();
  threads.clear();

  // Then evacuate all hot objs.
  evacuator.parallel_gc(kNumGCThds);
  evacuator.parallel_gc(kNumGCThds);

  if (nr_errs == 0)
    std::cout << "Test passed!" << std::endl;

  return 0;
}