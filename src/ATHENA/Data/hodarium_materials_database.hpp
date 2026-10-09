/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "materials.hpp"

namespace athena::hodarium {
enum class material_object_kind { record, attachment, relation, alias };
struct material_object {
  material_object_kind kind;
  MaterialRecord record;
  MaterialAttachment attachment;
  MaterialRelation relation;
  std::string alias, canonical;
};
bool local_material_provenance (const MaterialProvenance& value);
logical_database_object encode_material_record (const MaterialRecord& record);
logical_database_object encode_material_attachment (const MaterialAttachment& attachment);
logical_database_object encode_material_relation (const MaterialRelation& relation);
logical_database_object encode_material_alias (const std::string& alias, const std::string& canonical);
material_object decode_material_object (const logical_database_object& object);
} // namespace athena::hodarium
