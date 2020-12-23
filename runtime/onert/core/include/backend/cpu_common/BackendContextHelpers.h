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

#ifndef __ONERT_BACKEND_CPU_COMMON_BACKEND_CONTEXT_HELPERS_H__
#define __ONERT_BACKEND_CPU_COMMON_BACKEND_CONTEXT_HELPERS_H__

#include <vector>

#include "ir/Index.h"
#include "compiler/GraphLowerInfo.h"
#include "util/logging.h"
#include "backend/ITensorRegistry.h"
#include "backend/BackendContext.h"
#include "Tensor.h"

namespace onert
{
namespace backend
{
namespace cpu_common
{

// TODO Remove the template param BackendContext once unification of cpu backend context is done
template <typename T_BackendContext>
void planTensors(const T_BackendContext &ctx, const std::vector<onert::ir::OperationIndex> &order)
{
  const ir::Graph &graph = *ctx.graph();
  auto tensor_builder = ctx.tensor_builder;

  ir::OperandIndexMap<uint32_t> uses_map;
  ir::OperandIndexMap<uint32_t> def_map;
  ir::OperandIndexSequence constants;

  auto model_io =
    (graph.getInputs() + graph.getOutputs()) | ir::Remove::UNDEFINED | ir::Remove::DUPLICATED;

  // Prepare scanning
  graph.operands().iterate([&](const ir::OperandIndex &ind, const ir::Operand &obj) {
    if (model_io.contains(ind))
      return;
    if (ctx.external_operands().contains(ind))
      return;

    uses_map[ind] = obj.getUses().size();
    def_map[ind] = obj.getDef().valid() ? 1 : 0;

    if (obj.isConstant())
      constants.append(ind);

    if (!tensor_builder->isRegistered(ind))
    {
      // These tensors do not exist in any  (No use and def)
      const auto info = obj.info();
      // NOTE Currently we only support NHWC tensors for cpu-common tensors.
      //      There is no way to get the layout info from the backend context for now.
      //      When we support NCHW tensors as well, we also need to change tensor info to be
      //      permuted shape.
      const auto backend_layout = ir::Layout::NHWC;
      tensor_builder->registerTensorInfo(ind, info, backend_layout);
    }
  });

  // Start scanning to do notify{First|Last}Use for each tensor

  // If a tensor is a constant, increase the use of the tensor and allocate it first.
  // Increasing use count here makes the tensor never be deallocated, i.e it they will be
  // deallocated last.
  for (const auto &ind : constants)
  {
    uses_map[ind]++;
    tensor_builder->notifyFirstUse(ind);
  }

  // At each operation,
  // 1. Scan DEF of outputs. If the DEF, allocate it
  // 2. Scan DEF of inputs. If variable tensor, allocate it
  // 3. Scan USE of inputs. Decrease the USE and deallocate if the USE is 0
  for (const auto op_ind : order)
  {
    // TODO Remove indentation
    {
      if (!graph.operations().exist(op_ind))
        continue;
      auto op_inputs =
        graph.operations().at(op_ind).getInputs() | ir::Remove::DUPLICATED | ir::Remove::UNDEFINED;
      auto op_outputs =
        graph.operations().at(op_ind).getOutputs() | ir::Remove::DUPLICATED | ir::Remove::UNDEFINED;

      // Define outputs
      for (const auto &ind : op_outputs)
      {
        if (model_io.contains(ind))
          continue;
        if (!tensor_builder->isRegistered(ind))
          continue;
        assert(def_map.find(ind) != def_map.end());
        if (def_map[ind])
        {
          def_map[ind] = 0;
          tensor_builder->notifyFirstUse(ind);
        }
      }

      // Scan variable tensors
      // This tensor has features like constant. But OperandInfo and LowerInfo treat them as
      // non-constant because of less memory usage by memory planning in here
      for (const auto &ind : op_inputs)
      {
        if (model_io.contains(ind))
          continue;
        if (!tensor_builder->isRegistered(ind))
          continue;
        const auto &operand = graph.operands().at(ind);
        if (operand.info().isVariable())
        {
          // The variable tensor with buffer is not supported yet
          assert(operand.data() == nullptr);
          assert(operand.getUses().size() == 1 && !operand.getDef().valid());
          assert(uses_map[ind] == 1 && def_map[ind] == 0);
          tensor_builder->notifyFirstUse(ind);
        }
      }

      for (const auto &ind : op_inputs)
      {
        if (model_io.contains(ind))
          continue;
        if (!tensor_builder->isRegistered(ind))
          continue;
        assert(uses_map.find(ind) != uses_map.end());
        assert(uses_map[ind] > 0);
        uses_map[ind]--;
        if (uses_map[ind] == 0)
        {
          // plan for deallocation of static tensornode
          tensor_builder->notifyLastUse(ind);

          // plan for deallocation of dynamic tensor
          auto dyn_tensor_manager = tensor_builder->dynamicTensorManager();
          auto *tensor = ctx.tensor_registry->getITensor(ind);
          assert(tensor);
          dyn_tensor_manager->planDealloc(op_ind, tensor);
        }
      }
    }
  }

  // Dispose and validate
  for (const auto &ind : constants)
  {
    --uses_map[ind];
    if (uses_map[ind] == 0) // To prevent notifyLastUse from being called twice
    {
      tensor_builder->notifyLastUse(ind);
    }
  }

  assert(
    std::all_of(uses_map.begin(), uses_map.end(),
                [](std::pair<const ir::OperandIndex, uint32_t> it) { return it.second == 0; }));

  assert(
    std::all_of(def_map.begin(), def_map.end(),
                [](std::pair<const ir::OperandIndex, uint32_t> it) { return it.second == 0; }));
}

template <typename T_BackendContext>
ITensorRegistry *genTensors(T_BackendContext &ctx,
                            const std::vector<onert::ir::OperationIndex> &order)
{
  const ir::Graph &graph = *ctx.graph();
  auto tensor_builder = ctx.tensor_builder;

  auto model_io =
    (graph.getInputs() + graph.getOutputs()) | ir::Remove::UNDEFINED | ir::Remove::DUPLICATED;
  graph.operands().iterate([&](const ir::OperandIndex &ind, const ir::Operand &obj) {
    if (model_io.contains(ind))
      return;
    if (ctx.external_operands().contains(ind))
      return;
    // NOTE Assuming there is no layout changes (Always assume NHWC or UNKNOWN)
    assert(graph.layout() != ir::Layout::NCHW);
    ir::OperandInfo backend_info{obj.shape(), obj.typeInfo(), obj.info().memAllocType(),
                                 obj.isConstant()};
    tensor_builder->registerTensorInfo(ind, backend_info, ir::Layout::NHWC);
  });

  // TODO Get compiler options from compiler, and use it rather than getting it from Env
  if (util::getConfigString(util::config::EXECUTOR) == "Linear")
  {
    cpu_common::planTensors(ctx, order);
  }
  else
  {
    // For the executors that does not have fixed linear execution order:
    // To make tensors never be deallocated, this is a workaround to use static memory planner
    graph.operands().iterate([&](const ir::OperandIndex &ind, const ir::Operand &) {
      if (tensor_builder->isRegistered(ind))
        tensor_builder->notifyFirstUse(ind);
    });
  }

  tensor_builder->allocate();

  return ctx.tensor_registry.get();
}

inline void initConsts(BackendContext &ctx)
{
  ctx.graph()->operands().iterate([&](const ir::OperandIndex &ind, const ir::Operand &operand) {
    if (ctx.external_operands().contains(ind) || !operand.isConstant())
      return;

    auto tensor = ctx.tensor_registry->getNativeITensor(ind);
    assert(tensor != nullptr);

    VERBOSE(FillOperandData) << "Fill data for " << ind << std::endl;

    auto data = operand.shareData();
    assert(data && data->base());
    ExternalTensor *ext_tensor = dynamic_cast<ExternalTensor *>(tensor);
    assert(ext_tensor);
    ext_tensor->setData(data);
  });
}

} // namespace cpu_common
} // namespace backend
} // namespace onert

#endif // __ONERT_BACKEND_CPU_COMMON_BACKEND_CONTEXT_HELPERS_H__
