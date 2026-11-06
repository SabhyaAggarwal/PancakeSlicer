#pragma once

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

#include "Exception.hpp"

namespace Slic3r {



inline constexpr size_t resolve_batch_size = 64;

















template<class ChunkSetup = std::monostate, class Resolve, class Commit>
void resolve_then_commit(size_t count, Resolve resolve, Commit commit)
{
    using Resolved = std::invoke_result_t<Resolve&, size_t>;

    std::vector<Resolved> resolved(std::min(count, resolve_batch_size));
    for (size_t first = 0; first < count; first += resolve_batch_size) {
        const size_t last = std::min(first + resolve_batch_size, count);


        tbb::this_task_arena::isolate([&] {
            tbb::parallel_for(tbb::blocked_range<size_t>(first, last),
                [&](const tbb::blocked_range<size_t>& range) {
                    ChunkSetup setup;
                    (void) setup;
                    for (size_t i = range.begin(); i < range.end(); ++ i)
                        resolved[i - first] = resolve(i);
                });
        });
        if (tbb::is_current_task_group_canceling())
            throw RuntimeError("resolve_then_commit: canceled before the batch was resolved");
        for (size_t i = first; i < last; ++ i)
            commit(i, std::move(resolved[i - first]));
    }
}

}
