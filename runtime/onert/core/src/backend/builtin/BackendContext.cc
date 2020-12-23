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

#include "BackendContext.h"

#include "KernelGenerator.h"
#include "backend/cpu_common/BackendContextHelpers.h"

namespace onert
{
namespace backend
{
namespace builtin
{

ITensorRegistry *BackendContext::genTensors(const std::vector<onert::ir::OperationIndex> &order)
{
  VERBOSE_F() << "BUILTIN GEN TENSORS" << std::endl;
  return cpu_common::genTensors(*this, order);
}

FunctionMap BackendContext::genKernels(const std::vector<ir::OperationIndex> &order)
{
  FunctionMap ret;

  for (auto op_ind : order)
  {
    // TODO Do not check if it exists, the caller must give order of operations that are present
    if (!graph()->operations().exist(op_ind))
      continue;
    auto fn_seq = kernel_gen->generate(op_ind);
    ret.emplace_back(op_ind, std::move(fn_seq));
  }

  cpu_common::initConsts(*this);

  // NOTE For memory optimization, we want to free some operand data
  const_cast<ir::Graph *>(graph())->operands().iterate(
    [&](const ir::OperandIndex &, ir::Operand &obj) { obj.releaseData(); });

  for (auto &it : ret)
  {
    auto &fn_seq = it.second;
    fn_seq->iterate([&](exec::IFunction &ifunc) { ifunc.prepare(); });
  }

  return ret;
}

} // namespace builtin
} // namespace backend
} // namespace onert
