/*******************************************************************************
* Copyright 2020-2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "oneapi/dnnl/dnnl_graph.hpp"
#include "oneapi/dnnl/dnnl_graph_types.h"

#include <gtest/gtest.h>

#include <vector>

TEST(api_tensor, create_with_shape) {
    using logical_tensor = dnnl::graph::logical_tensor;
    using data_type = logical_tensor::data_type;
    using tensor = dnnl::graph::tensor;
    using layout_type = logical_tensor::layout_type;
    const size_t id = 123;

    dnnl::graph::engine eng {dnnl::graph::engine::kind::cpu, 0};

    // 0D
    logical_tensor lt_0 {id, data_type::f32, logical_tensor::dims_t {},
            layout_type::strided};
    int n0 = 0;
    void *handle0 = &n0;
    tensor t_0 {lt_0, eng, handle0};
    ASSERT_EQ(t_0.get_data_handle<float>(), handle0);
    ASSERT_EQ(t_0.get_element_num(), 0);
    ASSERT_EQ(t_0.get_engine().get_kind(), eng.get_kind());
    ASSERT_EQ(t_0.get_engine().get_device_id(), eng.get_device_id());

    // 1D
    logical_tensor lt_1 {id, data_type::f32, logical_tensor::dims_t {3},
            layout_type::strided};
    std::vector<float> n1 {0, 1, 2};
    void *handle1 = n1.data();
    tensor t_1 {lt_1, eng, handle1};
    ASSERT_EQ(t_1.get_data_handle<float>(), handle1);
    ASSERT_EQ(t_1.get_element_num(), 3);
    ASSERT_EQ(t_0.get_engine().get_kind(), eng.get_kind());
    ASSERT_EQ(t_0.get_engine().get_device_id(), eng.get_device_id());

    // 2D
    logical_tensor lt_2 {id, data_type::f32, logical_tensor::dims_t {3, 4},
            layout_type::strided};
    std::vector<float> n2;
    n2.resize(3 * 4);
    void *handle2 = n2.data();
    tensor t_2 {lt_2, eng, handle2};
    ASSERT_EQ(t_2.get_data_handle<float>(), handle2);
    ASSERT_EQ(t_2.get_element_num(), 3 * 4);
    ASSERT_EQ(t_0.get_engine().get_kind(), eng.get_kind());
    ASSERT_EQ(t_0.get_engine().get_device_id(), eng.get_device_id());

    // 3D
    logical_tensor lt_3 {id, data_type::f32, logical_tensor::dims_t {3, 4, 5},
            layout_type::strided};
    std::vector<float> n3;
    n3.resize(3 * 4 * 5);
    void *handle3 = n3.data();
    tensor t_3 {lt_3, eng, handle3};
    ASSERT_EQ(t_3.get_data_handle<float>(), handle3);
    ASSERT_EQ(t_3.get_element_num(), 3 * 4 * 5);
    ASSERT_EQ(t_0.get_engine().get_kind(), eng.get_kind());
    ASSERT_EQ(t_0.get_engine().get_device_id(), eng.get_device_id());

    // 4D
    logical_tensor lt_4 {id, data_type::f32,
            logical_tensor::dims_t {3, 4, 5, 6}, layout_type::strided};
    std::vector<float> n4;
    n4.resize(3 * 4 * 5 * 6);
    void *handle4 = n4.data();
    tensor t_4 {lt_4, eng, handle4};
    ASSERT_EQ(t_4.get_data_handle<float>(), handle4);
    ASSERT_EQ(t_4.get_element_num(), 3 * 4 * 5 * 6);
    ASSERT_EQ(t_0.get_engine().get_kind(), eng.get_kind());
    ASSERT_EQ(t_0.get_engine().get_device_id(), eng.get_device_id());

    std::vector<float> n5 {0};
    void *handle5 = n5.data();
    tensor t_5 {lt_0, eng, nullptr};
    t_5.set_data_handle(handle5);
    ASSERT_EQ(t_5.get_data_handle<float>(), handle5);
}

TEST(api_tensor, shallow_copy) {
    using logical_tensor = dnnl::graph::logical_tensor;
    using tensor = dnnl::graph::tensor;
    using layout_type = logical_tensor::layout_type;

    dnnl::graph::engine eng {dnnl::graph::engine::kind::cpu, 0};

    const size_t id = 123;
    logical_tensor lt_1 {
            id, logical_tensor::data_type::f32, layout_type::strided};

    int n = 0;
    void *handle = &n;

    tensor t_1 {lt_1, eng, handle};
    tensor t_2(t_1);

    ASSERT_EQ(t_2.get_data_handle<float>(), handle);
    ASSERT_EQ(t_1.get_element_num(), t_2.get_element_num());
}

TEST(tensor_test, create_with_logical_tensor_f32) {
    using namespace dnnl::graph;
    engine eng {engine::kind::cpu, 0};

    logical_tensor lt {0, logical_tensor::data_type::f32,
            logical_tensor::layout_type::any};
    tensor t {lt, eng, nullptr};

    ASSERT_EQ(t.get_element_num(), -1);
}

TEST(tensor_test, create_with_logical_tensor_s8) {
    using namespace dnnl::graph;
    engine eng {engine::kind::cpu, 0};

    logical_tensor lt {
            0, logical_tensor::data_type::s8, logical_tensor::layout_type::any};
    tensor t {lt, eng, nullptr};

    ASSERT_EQ(t.get_element_num(), -1);
}