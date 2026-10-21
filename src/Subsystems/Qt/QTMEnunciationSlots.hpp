#pragma once
#include "actor_transport.hpp"
#include <string>
class QTMWidget;
void qtm_enunciation_slot (QTMWidget*, athena_actor_id, athena_view_id,
                           const std::string& request);
