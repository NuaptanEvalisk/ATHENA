/******************************************************************************
* MODULE     : athena_diff.cpp
* DESCRIPTION: Structural comparison of ATHENA document trees
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/athena_diff.hpp"

#include "analyze.hpp"
#include "tree_cursor.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr uint64_t max_lcs_cells= 2000000;

void
append_range (range_set& ranges, path first, path last) {
  if (first == last) return;
  ranges << first << last;
}

void
append_subtree (range_set& ranges, tree value, path position) {
  append_range (ranges, position * start (value), position * end (value));
}

class StructuralDiffer {
public:
  AthenaTreeDiff result;

  void compare (tree left, tree right, path leftPath= path (),
                path rightPath= path ()) {
    if (left == right) return;
    if (is_atomic (left) && is_atomic (right)) {
      compareText (left->label, right->label, leftPath, rightPath);
      return;
    }
    if (is_atomic (left) || is_atomic (right) || L(left) != L(right)) {
      markSubtrees (left, right, leftPath, rightPath);
      return;
    }
    if (isSequence (left)) {
      compareSequence (left, right, leftPath, rightPath, 0, N(left), 0,
                       N(right));
      return;
    }
    if (N(left) != N(right)) {
      markSubtrees (left, right, leftPath, rightPath);
      return;
    }
    for (int i=0; i<N(left); ++i)
      compare (left[i], right[i], leftPath * i, rightPath * i);
  }

private:
  static bool isSequence (tree value) {
    return is_func (value, DOCUMENT) || is_func (value, CONCAT);
  }

  void markSubtrees (tree left, tree right, path leftPath, path rightPath) {
    append_subtree (result.left, left, leftPath);
    append_subtree (result.right, right, rightPath);
    ++result.hunks;
  }

  void markLeft (tree value, path position) {
    append_subtree (result.left, value, position);
    ++result.hunks;
  }

  void markRight (tree value, path position) {
    append_subtree (result.right, value, position);
    ++result.hunks;
  }

  void compareText (string left, string right, path leftPath, path rightPath) {
    array<int> changes= differences (left, right);
    for (int i=0; i+3<N(changes); i+=4) {
      append_range (result.left, leftPath * changes[i],
                    leftPath * changes[i + 1]);
      append_range (result.right, rightPath * changes[i + 2],
                    rightPath * changes[i + 3]);
      ++result.hunks;
    }
  }

  static bool equalChild (tree left, tree right, int leftHash,
                          int rightHash) {
    return leftHash == rightHash && left == right;
  }

  static std::vector<std::pair<int, int>> boundedLcs (
      tree left, tree right, int leftBegin, int leftEnd,
      int rightBegin, int rightEnd, const std::vector<int>& leftHashes,
      const std::vector<int>& rightHashes) {
    int leftCount= leftEnd - leftBegin;
    int rightCount= rightEnd - rightBegin;
    std::vector<uint32_t> lengths (
      (size_t) (leftCount + 1) * (size_t) (rightCount + 1), 0);
    auto at= [&] (int i, int j) -> uint32_t& {
      return lengths[(size_t) i * (size_t) (rightCount + 1) + (size_t) j];
    };
    for (int i=leftCount - 1; i>=0; --i)
      for (int j=rightCount - 1; j>=0; --j) {
        int li= leftBegin + i;
        int rj= rightBegin + j;
        if (equalChild (left[li], right[rj], leftHashes[(size_t) li],
                        rightHashes[(size_t) rj]))
          at (i, j)= at (i + 1, j + 1) + 1;
        else at (i, j)= std::max (at (i + 1, j), at (i, j + 1));
      }

    std::vector<std::pair<int, int>> matches;
    for (int i=0, j=0; i<leftCount && j<rightCount; ) {
      int li= leftBegin + i;
      int rj= rightBegin + j;
      if (equalChild (left[li], right[rj], leftHashes[(size_t) li],
                      rightHashes[(size_t) rj])) {
        matches.emplace_back (li, rj);
        ++i;
        ++j;
      }
      else if (at (i + 1, j) >= at (i, j + 1)) ++i;
      else ++j;
    }
    return matches;
  }

  static std::vector<std::pair<int, int>> patienceAnchors (
      tree left, tree right, int leftBegin, int leftEnd,
      int rightBegin, int rightEnd, const std::vector<int>& leftHashes,
      const std::vector<int>& rightHashes) {
    struct Occurrence { int count= 0; int index= -1; };
    std::unordered_map<int, Occurrence> leftOccurrences;
    std::unordered_map<int, Occurrence> rightOccurrences;
    for (int i=leftBegin; i<leftEnd; ++i) {
      Occurrence& occurrence= leftOccurrences[leftHashes[(size_t) i]];
      ++occurrence.count;
      occurrence.index= i;
    }
    for (int i=rightBegin; i<rightEnd; ++i) {
      Occurrence& occurrence= rightOccurrences[rightHashes[(size_t) i]];
      ++occurrence.count;
      occurrence.index= i;
    }

    std::vector<std::pair<int, int>> candidates;
    for (int i=leftBegin; i<leftEnd; ++i) {
      int key= leftHashes[(size_t) i];
      auto rightHit= rightOccurrences.find (key);
      if (leftOccurrences[key].count != 1 ||
          rightHit == rightOccurrences.end () || rightHit->second.count != 1)
        continue;
      int j= rightHit->second.index;
      if (left[i] == right[j]) candidates.emplace_back (i, j);
    }

    std::vector<int> tails;
    std::vector<int> tailCandidates;
    std::vector<int> previous (candidates.size (), -1);
    for (size_t i=0; i<candidates.size (); ++i) {
      int rightIndex= candidates[i].second;
      auto hit= std::lower_bound (tails.begin (), tails.end (), rightIndex);
      int length= (int) (hit - tails.begin ());
      if (hit == tails.end ()) {
        tails.push_back (rightIndex);
        tailCandidates.push_back ((int) i);
      }
      else {
        *hit= rightIndex;
        tailCandidates[(size_t) length]= (int) i;
      }
      if (length > 0)
        previous[i]= tailCandidates[(size_t) length - 1];
    }

    std::vector<std::pair<int, int>> matches;
    if (tailCandidates.empty ()) return matches;
    for (int index= tailCandidates.back (); index >= 0;
         index= previous[(size_t) index])
      matches.push_back (candidates[(size_t) index]);
    std::reverse (matches.begin (), matches.end ());
    return matches;
  }

  void compareUnmatched (tree left, tree right, path leftPath, path rightPath,
                         int leftBegin, int leftEnd, int rightBegin,
                         int rightEnd) {
    int leftCount= leftEnd - leftBegin;
    int rightCount= rightEnd - rightBegin;
    if (leftCount == rightCount) {
      for (int i=0; i<leftCount; ++i)
        compare (left[leftBegin + i], right[rightBegin + i],
                 leftPath * (leftBegin + i), rightPath * (rightBegin + i));
      return;
    }
    for (int i=leftBegin; i<leftEnd; ++i)
      markLeft (left[i], leftPath * i);
    for (int i=rightBegin; i<rightEnd; ++i)
      markRight (right[i], rightPath * i);
  }

  void compareSequence (tree left, tree right, path leftPath, path rightPath,
                        int leftBegin, int leftEnd, int rightBegin,
                        int rightEnd) {
    while (leftBegin < leftEnd && rightBegin < rightEnd &&
           left[leftBegin] == right[rightBegin]) {
      ++leftBegin;
      ++rightBegin;
    }
    while (leftBegin < leftEnd && rightBegin < rightEnd &&
           left[leftEnd - 1] == right[rightEnd - 1]) {
      --leftEnd;
      --rightEnd;
    }
    if (leftBegin == leftEnd || rightBegin == rightEnd) {
      compareUnmatched (left, right, leftPath, rightPath, leftBegin, leftEnd,
                        rightBegin, rightEnd);
      return;
    }

    std::vector<int> leftHashes ((size_t) N(left));
    std::vector<int> rightHashes ((size_t) N(right));
    for (int i=leftBegin; i<leftEnd; ++i)
      leftHashes[(size_t) i]= hash (left[i]);
    for (int i=rightBegin; i<rightEnd; ++i)
      rightHashes[(size_t) i]= hash (right[i]);

    uint64_t cells= (uint64_t) (leftEnd - leftBegin) *
                    (uint64_t) (rightEnd - rightBegin);
    std::vector<std::pair<int, int>> matches=
      cells <= max_lcs_cells
        ? boundedLcs (left, right, leftBegin, leftEnd, rightBegin, rightEnd,
                      leftHashes, rightHashes)
        : patienceAnchors (left, right, leftBegin, leftEnd, rightBegin,
                           rightEnd, leftHashes, rightHashes);
    if (matches.empty ()) {
      compareUnmatched (left, right, leftPath, rightPath, leftBegin, leftEnd,
                        rightBegin, rightEnd);
      return;
    }

    int nextLeft= leftBegin;
    int nextRight= rightBegin;
    for (const auto& match: matches) {
      compareUnmatched (left, right, leftPath, rightPath, nextLeft,
                        match.first, nextRight, match.second);
      nextLeft= match.first + 1;
      nextRight= match.second + 1;
    }
    compareUnmatched (left, right, leftPath, rightPath, nextLeft, leftEnd,
                      nextRight, rightEnd);
  }
};

} // namespace

AthenaTreeDiff
athena_diff_trees (tree left, tree right) {
  StructuralDiffer differ;
  differ.compare (left, right);
  return differ.result;
}
