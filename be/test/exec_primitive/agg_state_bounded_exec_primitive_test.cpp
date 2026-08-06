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

#include "exec/aggregate/agg_state_bounded.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace starrocks {
namespace {

TExpr make_agg_expr(const std::string& function_name) {
    TFunctionName fn_name;
    fn_name.__set_function_name(function_name);
    TFunction fn;
    fn.__set_name(fn_name);
    TExprNode node;
    node.__set_fn(fn);
    TExpr expr;
    expr.nodes.push_back(node);
    return expr;
}

std::vector<TExpr> make_agg_exprs(const std::vector<std::string>& names) {
    std::vector<TExpr> exprs;
    exprs.reserve(names.size());
    for (const auto& name : names) {
        exprs.push_back(make_agg_expr(name));
    }
    return exprs;
}

} // namespace

TEST(AggStateBoundedTest, single_value_states_are_bounded) {
    for (const std::string name : {"count", "sum", "avg", "min", "max", "any_value"}) {
        EXPECT_TRUE(all_agg_states_bounded(make_agg_exprs({name}))) << name;
    }
    EXPECT_TRUE(all_agg_states_bounded(make_agg_exprs({"count", "sum", "max"})));
}

TEST(AggStateBoundedTest, collect_style_states_are_unbounded) {
    for (const std::string name : {"array_agg", "group_concat", "multi_distinct_count", "multi_distinct_sum",
                                   "bitmap_union", "bitmap_agg", "hll_raw_agg", "percentile_disc", "map_agg"}) {
        EXPECT_FALSE(all_agg_states_bounded(make_agg_exprs({name}))) << name;
    }
}

TEST(AggStateBoundedTest, one_unbounded_state_taints_the_whole_list) {
    EXPECT_FALSE(all_agg_states_bounded(make_agg_exprs({"sum", "count", "array_agg"})));
}

TEST(AggStateBoundedTest, unknown_functions_are_treated_as_unbounded) {
    EXPECT_FALSE(all_agg_states_bounded(make_agg_exprs({"some_future_agg_func"})));
}

TEST(AggStateBoundedTest, empty_function_list_is_bounded) {
    EXPECT_TRUE(all_agg_states_bounded({}));
}

TEST(AggStateBoundedTest, malformed_exprs_are_treated_as_unbounded) {
    TExpr no_nodes;
    EXPECT_FALSE(all_agg_states_bounded({no_nodes}));

    TExprNode bare_node;
    TExpr no_fn;
    no_fn.nodes.push_back(bare_node);
    EXPECT_FALSE(all_agg_states_bounded({no_fn}));
}

} // namespace starrocks
