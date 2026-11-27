#pragma once

#include <iosfwd>

#include "core/Config.h"

namespace agentsim::app {

int runResearchEpisode(const SimConfig& config, bool audit, std::istream& in, std::ostream& out);

} // namespace agentsim::app
