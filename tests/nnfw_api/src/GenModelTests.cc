/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file This file contains miscellaneous GenModelTest test cases.
 *
 */

#include "GenModelTest.h"

#include <memory>

TEST_F(GenModelTest, UnusedConstOutputOnly)
{
  // A single tensor which is constant
  CircleGen cgen;
  uint32_t const_buf = cgen.addBuffer(std::vector<float>{9, 8, 7, 6});
  int out_const = cgen.addTensor({{1, 2, 2, 1}, circle::TensorType::TensorType_FLOAT32, const_buf});
  cgen.setInputsAndOutputs({}, {out_const});

  _context = std::make_unique<GenModelTestContext>(cgen.finish());
  _context->addTestCase(uniformTCD<float>({}, {{9, 8, 7, 6}}));
  _context->setBackends({"acl_cl", "acl_neon", "cpu"});

  SUCCEED();
}

TEST_F(GenModelTest, UnusedConstOutputAndAdd)
{
  // A single tensor which is constant + an Add op
  CircleGen cgen;
  uint32_t rhs_buf = cgen.addBuffer(std::vector<float>{5, 4, 7, 4});
  uint32_t const_buf = cgen.addBuffer(std::vector<float>{9, 8, 7, 6});
  int lhs = cgen.addTensor({{1, 2, 2, 1}, circle::TensorType::TensorType_FLOAT32});
  int rhs = cgen.addTensor({{1, 2, 2, 1}, circle::TensorType::TensorType_FLOAT32, rhs_buf});
  int out = cgen.addTensor({{1, 2, 2, 1}, circle::TensorType::TensorType_FLOAT32});
  int out_const = cgen.addTensor({{1, 2, 2, 1}, circle::TensorType::TensorType_FLOAT32, const_buf});
  cgen.addOperatorAdd({{lhs, rhs}, {out}}, circle::ActivationFunctionType_NONE);
  cgen.setInputsAndOutputs({lhs}, {out, out_const});

  _context = std::make_unique<GenModelTestContext>(cgen.finish());
  _context->addTestCase(uniformTCD<float>({{1, 3, 2, 4}}, {{6, 7, 9, 8}, {9, 8, 7, 6}}));
  _context->addTestCase(uniformTCD<float>({{0, 1, 2, 3}}, {{5, 5, 9, 7}, {9, 8, 7, 6}}));
  _context->setBackends({"acl_cl", "acl_neon", "cpu"});

  SUCCEED();
}
