#include <atomic>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "soft_mem_pool.hpp"
#include "soft_unique_ptr.hpp"
#include "smdk_opt_api.hpp"

constexpr static int kNumThds = 1;
constexpr static int kObjSize = 64;
constexpr static int kNumObjs = 10240;

struct Object {
  int data[kObjSize / sizeof(int)];
};

int recon_int(int a, float d) { return a + int(d); }

struct Object recon_obj(int a, uint64_t b) {
  struct Object obj;
  int c = static_cast<int>(b & 0xFFFFFFFF);
  for (int i = 0; i < kObjSize / sizeof(int); i++) {
    obj.data[i] = a + c + i;
  }
  return obj;
}

void test_int_read() {
  auto smanager = midas::SoftMemPoolManager::global_soft_mem_pool_manager();
  auto pool = smanager->get_pool<int, int, float>("int");

  std::vector<std::thread> thds;
  std::atomic_int32_t nr_failed(0);

  for (int i = 0; i < kNumThds; i++) { // 每个thread要进行一次初始化
    thds.emplace_back([&pool, &nr_failed]() {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist(0, 1 << 30);
      using SoftUniquePtr = midas::SoftUniquePtr<int, int, float>;
      std::vector<SoftUniquePtr> ptrs;

      for (int j = 0; j < kNumObjs; j++) {
        auto ptr = pool->new_unique();
        ptrs.push_back(std::move(ptr));
      }
      for (int j = 0; j < kNumObjs; j++) {
        int a = dist(mt);
        float b = dist(mt);
        int recon_value = recon_int(a, b);
        int value = ptrs[j].read(a, b);
        if (value != recon_value)
          nr_failed++;
      }
      for (int j = 0; j < kNumObjs; j++) {
        ptrs[j].reset();
      }
    });
  }
  for (auto &thd : thds)
    thd.join();

  if (nr_failed == 0) {
    std::cout << "Test int read passed." << std::endl;
  } else {
    std::cout << "Test int read failed " << nr_failed << std::endl;
  }
}


void test_int_write() {
  std::cout << "init smanager" << std::endl;
  auto smanager = midas::SoftMemPoolManager::global_soft_mem_pool_manager();
  std::cout << "init pool" << std::endl;
  auto pool = smanager->get_pool<int, int, float>("int");
  std::cout << "init thread" << std::endl;

  std::vector<std::thread> thds;
  std::atomic_int32_t nr_failed(0);

  for (int i = 0; i < kNumThds; i++) {
    thds.emplace_back([&pool, &nr_failed]() {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist(0, 1 << 30);
      using SoftUniquePtr = midas::SoftUniquePtr<int, int, float>;
      std::vector<SoftUniquePtr> ptrs;
      std::vector<int> written_vals;

      std::cout << "1" << std::endl;
      for (int j = 0; j < kNumObjs; j++) { // 这里出现了真正的内存分配行为
        auto ptr = pool->new_unique();
        // std::cout << "new_unique write" << std::endl;
        int value = dist(mt);
        ptr.write(value); // 这句实际产生了分配行为，注释掉了就没了
        ptrs.push_back(std::move(ptr));
        written_vals.push_back(value);
      }
      std::cout << "2" << std::endl;
      // for (int j = 0; j < kNumObjs; j++) {
      //   int a = dist(mt);
      //   float b = dist(mt);
      //   int written_value = written_vals[j];
      //   int value = ptrs[j].read(a, b);
      //   if (value != written_value) {
      //     int recon_value = recon_int(a, b);
      //     std::cout << "value: " << value << " written_value: " << written_value << " recon_value: " << recon_value
      //               << std::endl;
      //     nr_failed++;
      //   }
      // }
      // for (int j = 0; j < kNumObjs; j++) {
      //   ptrs[j].reset();
      // }
    });
  }
  for (auto &thd : thds)
    thd.join();
  if (nr_failed == 0) {
    std::cout << "Test int write passed." << std::endl;
  } else {
    std::cout << "Test int write failed " << nr_failed << std::endl;
  }
}

void test_obj_read() {
  auto smanager = midas::SoftMemPoolManager::global_soft_mem_pool_manager();
  auto pool = smanager->get_pool<struct Object, int, uint64_t>("object");

  std::vector<std::thread> thds;
  std::atomic_int32_t nr_failed(0);

  for (int i = 0; i < kNumThds; i++) {
    thds.emplace_back([&pool, &nr_failed]() {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist(0, 1 << 30);
      using SoftUniquePtr = midas::SoftUniquePtr<struct Object, int, uint64_t>;
      std::vector<SoftUniquePtr> ptrs;

      for (int j = 0; j < kNumObjs; j++) {
        auto ptr = pool->new_unique();
        ptrs.push_back(std::move(ptr));
      }
      for (int j = 0; j < kNumObjs; j++) {
        int a = dist(mt);
        uint64_t b = dist(mt);
        struct Object recon_value = recon_obj(a, b);
        struct Object value = ptrs[j].read(a, b);
        for (int k = 0; k < kObjSize / sizeof(int); k++) {
          if (value.data[k] != recon_value.data[k]) {
            nr_failed++;
            break;
          }
        }
      }
      for (int j = 0; j < kNumObjs; j++) {
        ptrs[j].reset();
      }
    });
  }
  for (auto &thd : thds)
    thd.join();
  if (nr_failed == 0) {
    std::cout << "Test obj read passed." << std::endl;
  } else {
    std::cout << "Test obj read failed " << nr_failed << std::endl;
  }
}

void test_obj_write() {
  auto smanager = midas::SoftMemPoolManager::global_soft_mem_pool_manager();
  auto pool = smanager->get_pool<struct Object, int, uint64_t>("object");

  std::vector<std::thread> thds;
  std::atomic_int32_t nr_failed(0);

  for (int i = 0; i < kNumThds; i++) {
    thds.emplace_back([&pool, &nr_failed]() {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist(0, 1 << 30);
      using SoftUniquePtr = midas::SoftUniquePtr<struct Object, int, uint64_t>;
      std::vector<SoftUniquePtr> ptrs;
      std::vector<struct Object> written_vals;

      for (int j = 0; j < kNumObjs; j++) {
        auto ptr = pool->new_unique();
        struct Object value;
        for (int k = 0; k < kObjSize / sizeof(int); k++) {
          value.data[k] = dist(mt);
        }
        ptr.write(value);
        ptrs.push_back(std::move(ptr));
        written_vals.push_back(value);
      }
      for (int j = 0; j < kNumObjs; j++) {
        int a = dist(mt);
        uint64_t b = dist(mt);
        struct Object written_value = written_vals[j];
        struct Object value = ptrs[j].read(a, b);
        for (int k = 0; k < kObjSize / sizeof(int); k++) {
          if (value.data[k] != written_value.data[k]) {
            struct Object recon_value = recon_obj(a, b);
            std::cout << "value: " << value.data[k] << " written_value: " << written_value.data[k]
                      << " recon_value: " << recon_value.data[k] << std::endl;
            nr_failed++;
            break;
          }
        }
      }
      for (int j = 0; j < kNumObjs; j++) {
        ptrs[j].reset();
      }
    });
  }
  for (auto &thd : thds)
    thd.join();

  if (nr_failed == 0) {
    std::cout << "Test obj write passed." << std::endl;
  } else {
    std::cout << "Test obj write failed " << nr_failed << std::endl;
  }
}

int main(int argc, char *argv[]) {

  auto smanager = midas::SoftMemPoolManager::global_soft_mem_pool_manager();

  smanager->create_pool<int, int, float>("int", recon_int);
  SmdkAllocator& allocator = SmdkAllocator::get_instance();
  std::cout << "1" << std::endl;
  allocator.stats_print('K');
  test_int_read();
  // SmdkAllocator& allocator = SmdkAllocator::get_instance();
  std::cout << "2" << std::endl;
  allocator.stats_print('K');
  test_int_write();

  std::cout << "3" << std::endl;
  allocator.stats_print('K');
  smanager->create_pool<Object, int, uint64_t>("object", recon_obj);
  std::cout << "4" << std::endl;
  allocator.stats_print('K');
  test_obj_read();
  std::cout << "5" << std::endl;
  allocator.stats_print('K');
  test_obj_write();
  std::cout << "6" << std::endl;
  allocator.stats_print('K');

  return 0;
}