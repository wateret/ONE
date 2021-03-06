/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "luci/IR/Nodes/CircleL2Pool2D.h"

#include "luci/IR/CircleDialect.h"
#include "luci/IR/CircleNodeVisitor.h"

#include <gtest/gtest.h>

TEST(CircleL2Pool2DTest, constructor)
{
  luci::CircleL2Pool2D l2pool2d_node;

  ASSERT_EQ(luci::CircleDialect::get(), l2pool2d_node.dialect());
  ASSERT_EQ(luci::CircleOpcode::L2_POOL_2D, l2pool2d_node.opcode());

  ASSERT_EQ(nullptr, l2pool2d_node.value());
  ASSERT_EQ(1, l2pool2d_node.filter()->h());
  ASSERT_EQ(1, l2pool2d_node.filter()->w());
  ASSERT_EQ(1, l2pool2d_node.stride()->h());
  ASSERT_EQ(1, l2pool2d_node.stride()->w());
  ASSERT_EQ(luci::FusedActFunc::UNDEFINED, l2pool2d_node.fusedActivationFunction());
}

TEST(CircleL2Pool2DTest, input_NEG)
{
  luci::CircleL2Pool2D l2pool2d_node;
  luci::CircleL2Pool2D node;

  l2pool2d_node.value(&node);
  ASSERT_NE(nullptr, l2pool2d_node.value());

  l2pool2d_node.value(nullptr);
  ASSERT_EQ(nullptr, l2pool2d_node.value());

  l2pool2d_node.stride()->h(2);
  l2pool2d_node.stride()->w(2);
  ASSERT_EQ(2, l2pool2d_node.stride()->h());
  ASSERT_EQ(2, l2pool2d_node.stride()->w());

  l2pool2d_node.filter()->h(2);
  l2pool2d_node.filter()->w(2);
  ASSERT_EQ(2, l2pool2d_node.filter()->h());
  ASSERT_EQ(2, l2pool2d_node.filter()->w());

  l2pool2d_node.fusedActivationFunction(luci::FusedActFunc::RELU);
  ASSERT_NE(luci::FusedActFunc::UNDEFINED, l2pool2d_node.fusedActivationFunction());
}

TEST(CircleL2Pool2DTest, arity_NEG)
{
  luci::CircleL2Pool2D l2pool2d_node;

  ASSERT_NO_THROW(l2pool2d_node.arg(0));
  ASSERT_THROW(l2pool2d_node.arg(1), std::out_of_range);
}

TEST(CircleL2Pool2DTest, visit_mutable_NEG)
{
  struct TestVisitor final : public luci::CircleNodeMutableVisitor<void>
  {
  };

  luci::CircleL2Pool2D l2pool2d_node;

  TestVisitor tv;
  ASSERT_THROW(l2pool2d_node.accept(&tv), std::exception);
}

TEST(CircleL2Pool2DTest, visit_NEG)
{
  struct TestVisitor final : public luci::CircleNodeVisitor<void>
  {
  };

  luci::CircleL2Pool2D l2pool2d_node;

  TestVisitor tv;
  ASSERT_THROW(l2pool2d_node.accept(&tv), std::exception);
}
