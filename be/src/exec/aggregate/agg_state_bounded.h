// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "gen_cpp/Exprs_types.h"

namespace starrocks {

// Aggregate functions whose per-group state stays bounded no matter how many input
// rows a group receives (single-value states). Collect-style states such as
// array_agg/group_concat/multi_distinct_count/bitmap_union grow with input rows, so a
// small limit cannot bound their memory usage and spill must stay available for them.
// Unknown or malformed function exprs are treated as unbounded so spill stays enabled.
inline bool all_agg_states_bounded(const std::vector<TExpr>& aggregate_functions) {
    static const std::unordered_set<std::string> BOUNDED_STATE_AGG_FUNCS = {"count", "sum",  "avg",
                                                                            "min",   "max", "any_value"};
    for (const auto& agg_expr : aggregate_functions) {
        if (agg_expr.nodes.empty() || !agg_expr.nodes[0].__isset.fn ||
            !BOUNDED_STATE_AGG_FUNCS.contains(agg_expr.nodes[0].fn.name.function_name)) {
            return false;
        }
    }
    return true;
}

} // namespace starrocks
