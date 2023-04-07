#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace midas {

constexpr static uint64_t to_us = 1000 * 1000; // 1s = 10^6 us
constexpr static uint64_t kMissDDL = 10ul * to_us; // 10s -> us

struct PerfRequest {
  virtual ~PerfRequest() = default;
};

struct PerfRequestWithTime {
  uint64_t start_us;
  std::unique_ptr<PerfRequest> req;
};

struct Trace {
  uint64_t absl_start_us;
  uint64_t start_us;
  uint64_t duration_us;
  Trace();
  Trace(uint64_t absl_start_us_, uint64_t start_us_, uint64_t duration_us_);
};

class PerfAdapter {
public:
  virtual std::unique_ptr<PerfRequest> gen_req(int tid) = 0;
  virtual bool serve_req(int tid, const PerfRequest *req) = 0;
};

// Closed-loop, possion arrival.
class Perf {
public:
  Perf(PerfAdapter &adapter);
  void reset();
  void run(uint32_t num_threads, double target_kops, uint64_t duration_us,
           uint64_t warmup_us = 0, uint64_t miss_ddl_thresh_us = kMissDDL);
  uint64_t get_average_lat();
  uint64_t get_nth_lat(double nth);
  std::vector<Trace> get_timeseries_nth_lats(uint64_t interval_us, double nth);
  double get_real_kops() const;
  const std::vector<Trace> &get_traces() const;

private:
  enum TraceFormat { kUnsorted, kSortedByDuration, kSortedByStart };

  PerfAdapter &adapter_;
  std::vector<Trace> traces_;
  TraceFormat trace_format_;
  double real_kops_;
  friend class Test;

  void gen_reqs(std::vector<PerfRequestWithTime> *all_reqs,
                uint32_t num_threads, double target_kops, uint64_t duration_us);
  std::vector<Trace> benchmark(std::vector<PerfRequestWithTime> *all_reqs,
                               uint32_t num_threads,
                               uint64_t miss_ddl_thresh_us);
};
} // namespace midas