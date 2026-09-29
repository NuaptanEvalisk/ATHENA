#include "ATHENA/Data/fossil_delta.hpp"

#include <cassert>
#include <iostream>
#include <string>

using athena::history::fossil_delta_apply;
using athena::history::fossil_delta_create;
using athena::history::fossil_delta_output_size;

int main () {
  // Fixed Fossil-format vector: target "abc" has big-endian checksum
  // 0x61626300, whose Fossil base-64 encoding is 1XObC0.
  const std::string fixed= "3\n3:abc1XObC0;";
  assert (fossil_delta_create ("", "abc") == fixed);
  auto fixed_result= fossil_delta_apply ("", fixed);
  assert (fixed_result && *fixed_result == "abc");

  const std::string source=
    "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const std::string target=
    "abcdefghijklmnop--changed--uvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const std::string delta= fossil_delta_create (source, target);
  assert (!delta.empty ());
  auto size= fossil_delta_output_size (delta);
  assert (size && *size == target.size ());
  auto rebuilt= fossil_delta_apply (source, delta);
  assert (rebuilt && *rebuilt == target);

  const std::string binary_source ("a\0b\1c\2d", 7);
  const std::string binary_target ("a\0B\1c\2d!", 8);
  auto binary= fossil_delta_apply (
    binary_source, fossil_delta_create (binary_source, binary_target));
  assert (binary && *binary == binary_target);

  std::string damaged= delta;
  damaged.back ()= '!';
  assert (!fossil_delta_apply (source, damaged));
  std::cout << "fossil_delta_test passed\n";
}
