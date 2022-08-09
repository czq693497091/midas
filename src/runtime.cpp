#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <ratio>
#include <string>
#include <sys/types.h>
#include <thread>

#include "utils.hpp"

namespace cachebank {

using SharedMemObj = boost::interprocess::shared_memory_object;
using MsgQueue = boost::interprocess::message_queue;
using MappedRegion = boost::interprocess::mapped_region;

static inline uint64_t get_unique_id() {
  auto pid = boost::interprocess::ipcdetail::get_current_process_id();
  auto creation_time =
      boost::interprocess::ipcdetail::get_current_process_creation_time();

  // TODO: unique id should be the hash of pid and creation_time to avoid pid
  // collision.
  return static_cast<uint64_t>(pid);
}

class Region {
public:
  Region(uint64_t pid, uint64_t region_id) noexcept
      : _pid(pid), _region_id(region_id), _alloc_bytes(0) {
    const auto rwmode = boost::interprocess::read_write;
    const std::string _shm_name = utils::get_region_name(_pid, _region_id);
    _shm_obj = std::make_shared<SharedMemObj>(boost::interprocess::open_only,
                                              _shm_name.c_str(), rwmode);
    _shm_region = std::make_shared<MappedRegion>(*_shm_obj, rwmode);
    _shm_obj->get_size(_size);
  }

  friend bool operator<(const Region &lhs, const Region &rhs) noexcept {
    return lhs._region_id < rhs._region_id;
  }

  inline void *Addr() const noexcept { return _shm_region->get_address(); }
  inline uint64_t ID() const noexcept { return _region_id; }
  inline int64_t Size() const noexcept { return _size; }
  inline int64_t GetAllocBytes() const noexcept { return _alloc_bytes; }

private:
  uint64_t _pid;
  uint64_t _region_id;
  std::shared_ptr<SharedMemObj> _shm_obj;
  std::shared_ptr<MappedRegion> _shm_region;
  int64_t _size; // int64_t to adapt to boost::interprocess::offset_t
  int64_t _alloc_bytes;
};

class ResourceManager {
public:
  ResourceManager(const std::string &daemon_name = kNameCtrlQ) noexcept {
    _id = get_unique_id();
    _ctrlq = std::make_shared<MsgQueue>(boost::interprocess::open_only,
                                        daemon_name.c_str());
    _sendq = std::make_shared<MsgQueue>(boost::interprocess::create_only,
                                        utils::get_sendq_name(_id).c_str(),
                                        kClientQDepth, sizeof(CtrlMsg));
    _recvq = std::make_shared<MsgQueue>(boost::interprocess::create_only,
                                        utils::get_recvq_name(_id).c_str(),
                                        kClientQDepth, sizeof(CtrlMsg));
    connect(daemon_name);
  };
  ~ResourceManager() noexcept {
    disconnect();
    MsgQueue::remove(utils::get_sendq_name(_id).c_str());
    MsgQueue::remove(utils::get_recvq_name(_id).c_str());
  }

  int AllocRegion(size_t size) noexcept;
  int FreeRegion(size_t size) noexcept;

private:
  int connect(const std::string &daemon_name = kNameCtrlQ) noexcept;
  int disconnect() noexcept;
  size_t free_Region(uint64_t region_id) noexcept;

  uint64_t _id;
  std::shared_ptr<MsgQueue> _ctrlq;
  std::shared_ptr<MsgQueue> _sendq;
  std::shared_ptr<MsgQueue> _recvq;

  std::map<uint64_t, std::shared_ptr<Region>> _region_map;
};

int ResourceManager::connect(const std::string &daemon_name) noexcept {
  try {
    unsigned int prio = 0;
    size_t recvd_size;
    CtrlMsg msg{.id = _id, .op = CtrlOpCode::CONNECT};

    _ctrlq->send(&msg, sizeof(CtrlMsg), prio);
    _recvq->receive(&msg, sizeof(CtrlMsg), recvd_size, prio);
    if (recvd_size != sizeof(CtrlMsg)) {
      return -1;
    }
    if (msg.op == CtrlOpCode::CONNECT && msg.ret == CtrlRetCode::CONN_SUCC)
      std::cout << "Connection established." << std::endl;
    else {
      std::cerr << "Connection failed." << std::endl;
      exit(1);
    }
  } catch (boost::interprocess::interprocess_exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}

int ResourceManager::disconnect() noexcept {
  try {
    unsigned int prio = 0;
    size_t recvd_size;
    CtrlMsg msg{.id = _id, .op = CtrlOpCode::DISCONNECT};

    _ctrlq->send(&msg, sizeof(CtrlMsg), prio);
    _recvq->receive(&msg, sizeof(CtrlMsg), recvd_size, prio);
    if (recvd_size != sizeof(CtrlMsg)) {
      return -1;
    }
    if (msg.op == CtrlOpCode::DISCONNECT && msg.ret == CtrlRetCode::CONN_SUCC)
      std::cout << "Connection destroyed." << std::endl;
    else {
      std::cerr << "Disconnection failed." << std::endl;
      return -1;
    }
  } catch (boost::interprocess::interprocess_exception &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}

int ResourceManager::AllocRegion(size_t size) noexcept {
  CtrlMsg msg{.id = _id,
              .op = CtrlOpCode::ALLOC,
              .mmsg = {.size = static_cast<int64_t>(size)}};
  _ctrlq->send(&msg, sizeof(msg), 0);

  unsigned prio;
  size_t recvd_size;
  CtrlMsg ret_msg;
  _recvq->receive(&ret_msg, sizeof(ret_msg), recvd_size, prio);
  if (recvd_size != sizeof(ret_msg)) {
    std::cerr << "ERROR [" << __LINE__ << "] : in recv msg, " << recvd_size
              << " != " << sizeof(ret_msg) << std::endl;
    return -1;
  }
  if (ret_msg.ret != CtrlRetCode::MEM_SUCC) {
    std::cerr << "ERROR in " << __LINE__ << std::endl;
    return -1;
  }

  assert(_region_map.find(ret_msg.mmsg.region_id) == _region_map.cend());

  auto region = std::make_shared<Region>(_id, ret_msg.mmsg.region_id);
  _region_map[ret_msg.mmsg.region_id] = region;
  assert(region->Size() == ret_msg.mmsg.size);

  std::cout << "Allocated a page chunk: " << region->Addr() << " ["
            << region->Size() << "]" << std::endl;
  return 0;
}

int ResourceManager::FreeRegion(size_t size) noexcept {
  size_t total_freed = 0;
  int nr_freed_chunks = 0;
  while (!_region_map.empty()) {
    auto region_iter = _region_map.begin();
    size_t freed_bytes = free_Region(region_iter->second->ID());
    total_freed += freed_bytes;
    nr_freed_chunks++;
    if (total_freed >= size)
      break;
  }
  std::cout << "Freed " << nr_freed_chunks << " page chunks (" << total_freed
            << "bytes)" << std::endl;
  return 0;
}

size_t ResourceManager::free_Region(uint64_t region_id) noexcept {
  size_t size = 0;
  auto region_iter = _region_map.find(region_id);
  if (region_iter == _region_map.cend()) {
    std::cerr << "Invalid region_id " << region_id << std::endl;
    return -1;
  }

  _region_map.erase(region_id);
  size = region_iter->second->Size();
  std::cout << "page_chunk_map size: " << _region_map.size() << std::endl;
  return size;
}
} // namespace cachebank

int main(int argc, char *argv[]) {
  cachebank::ResourceManager rmanager;

  for (int i = 0; i < 10; i++) {
    rmanager.AllocRegion(cachebank::kPageChunkSize);
  }
  for (int i = 0; i < 10; i++) {
    rmanager.FreeRegion(cachebank::kPageChunkSize);
  }
  return 0;
}