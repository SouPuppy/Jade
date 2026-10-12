#pragma once
#include <Jade/Jade.hpp>
namespace Untitled {
struct BallVisual { int representation = 0; };
void configureWorld(Jade::World&, Jade::Assets&, const std::filesystem::path& worldRoot,
                    std::shared_ptr<Jade::WorldStore>, Jade::JobSystem*);
}
