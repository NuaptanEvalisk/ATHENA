#pragma once
#include "actor_transport.hpp"
#include <string>
#include <memory>
namespace athena::contracts { class detached_environment; }
class QTMWidget;
void qtm_enunciation_slot (QTMWidget*, athena_actor_id, athena_view_id,
                           const std::string& request,
                           std::shared_ptr<const athena::contracts::detached_environment> environment);
