/******************************************************************************
* MODULE     : thumbnail_snapshot.hpp
* DESCRIPTION: POD page geometry published by an owning BufferActor
******************************************************************************/
#ifndef ATHENA_THUMBNAIL_SNAPSHOT_HPP
#define ATHENA_THUMBNAIL_SNAPSHOT_HPP

#include <cstdint>

struct thumbnail_page_bounds {
  std::int64_t x1= 0, y1= 0, x2= 0, y2= 0;
};

struct thumbnail_manifest_header {
  std::uint64_t signature= 0;
  std::uint64_t style_signature= 0;
  std::uint64_t content_signature= 0;
  std::uint32_t count= 0;
  std::uint32_t active_page= 0;
  std::uint32_t flags= 0;
  std::uint32_t reserved= 0;
};

constexpr std::uint32_t THUMBNAIL_MANIFEST_SLIDES= 1U;

#endif
