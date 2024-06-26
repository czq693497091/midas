/**
 * Copied from AIFM.
 */

#pragma once

#include <limits>
#include <random>
#include <vector>

namespace midas {

/**
 * Example usage:
 *
 *    std::random_device rd;
 *    std::mt19937 gen(rd());
 *    zipf_table_distribution<> zipf(300);
 *
 *    for (int i = 0; i < 100; i++)
 *        printf("draw %d %d\n", i, zipf(gen));
 */
template <class IntType = unsigned long, class RealType = double>
class zipf_table_distribution {
public:
  typedef IntType result_type;

  static_assert(std::numeric_limits<IntType>::is_integer, "");
  static_assert(!std::numeric_limits<RealType>::is_integer, "");

  /// zipf_table_distribution(N, s)
  /// Zipf distribution for `N` items, in the range `[1,N]` inclusive.
  /// The distribution follows the power-law 1/n^s with exponent `s`.
  /// This uses a table-lookup, and thus provides values more
  /// quickly than zipf_distribution. However, the table can take
  /// up a considerable amount of RAM, and initializing this table
  /// can consume significant time.
  zipf_table_distribution(const IntType n, const RealType q = 1.0);
  void reset();
  IntType operator()(std::mt19937 &rng);
  /// Returns the parameter the distribution was constructed with.
  RealType s() const;
  /// Returns the minimum value potentially generated by the distribution.
  result_type min() const;
  /// Returns the maximum value potentially generated by the distribution.
  result_type max() const;

private:
  std::vector<RealType> pdf_;                ///< Prob. distribution
  IntType n_;                                ///< Number of elements
  RealType q_;                               ///< Exponent
  std::discrete_distribution<IntType> dist_; ///< Draw generator

  /** Initialize the probability mass function */
  IntType init(const IntType n, const RealType q);
};
} // namespace midas

#include "impl/zipf.ipp"
