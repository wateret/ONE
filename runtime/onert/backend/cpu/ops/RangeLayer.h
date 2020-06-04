/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in riting, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __ONERT_BACKEND_CPU_OPS_RANGELAYER_H__
#define __ONERT_BACKEND_CPU_OPS_RANGELAYER_H__

#include "../Tensor.h"

#include <exec/IFunction.h>

namespace onert
{
namespace backend
{
namespace cpu
{
namespace ops
{
class RangeLayer : public ::onert::exec::IFunction
{
public:
  RangeLayer();

  void configure(const Tensor *start, const Tensor *limit, const Tensor *delta, Tensor *output);

  void run();

private:
  const Tensor *_start;
  const Tensor *_limit;
  const Tensor *_delta;
  Tensor *_output;
};

} // namespace ops
} // namespace cpu
} // namespace backend
} // namespace onert

#endif // __ONERT_BACKEND_CPU_OPS_RANGELAYER_H__
