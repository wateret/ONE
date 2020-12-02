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

#ifndef __ONERT_BACKEND_RUY_KERNEL_GENERATOR_H__
#define __ONERT_BACKEND_RUY_KERNEL_GENERATOR_H__

#include "ExternalContext.h"
#include "TensorBuilder.h"
#include "backend/cpu_common/TensorRegistry.h"
#include "Tensor.h"

#include <backend/CustomKernelBuilder.h>
#include <backend/cpu_common/KernelGeneratorBase.h>
#include <ir/Operands.h>
#include <ir/Operations.h>

namespace onert
{
namespace backend
{
namespace ruy
{

class KernelGenerator : public cpu_common::KernelGeneratorBase
{
public:
  KernelGenerator(const ir::Operands &operands_ctx, const ir::Operations &operations_ctx,
                  const std::shared_ptr<TensorBuilder> &tensor_builder,
                  const std::shared_ptr<cpu_common::TensorRegistry> &tensor_reg,
                  const std::shared_ptr<custom::IKernelBuilder> &kernel_builder,
                  const std::shared_ptr<ExternalContext> &external_context);

  void visit(const ir::OpSequence &) override;
  void visit(const ir::operation::Conv2D &) override;
  void visit(const ir::operation::FullyConnected &) override;

private:
  const ir::Operands &_ctx;
  const ir::Operations &_operations_ctx;
  std::shared_ptr<TensorBuilder> _tensor_builder;
  std::shared_ptr<cpu_common::TensorRegistry> _tensor_reg;
  std::shared_ptr<backend::custom::IKernelBuilder> _kernel_builder;
  ir::Layout _current_layout;
  const std::shared_ptr<ExternalContext> _external_context;
};

} // namespace ruy
} // namespace backend
} // namespace onert

#endif // __ONERT_BACKEND_RUY_KERNEL_GENERATOR_H__
