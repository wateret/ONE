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

#ifndef __ONERT_BACKEND_BACKEND_CONTEXT_H__
#define __ONERT_BACKEND_BACKEND_CONTEXT_H__

#include <memory>
#include "ir/Graph.h"
#include "ir/LowerInfoMap.h"
#include "exec/FunctionSequence.h"

namespace onert
{
namespace backend
{

class Backend;
struct ITensorRegistry;

using FunctionMap =
  std::vector<std::pair<ir::OpSequenceIndex, std::unique_ptr<exec::FunctionSequence>>>;

struct ContextData
{
  std::unique_ptr<ir::Graph> graph;
  // XXX Consider adding these
  // std::vector<onert::ir::OpSequenceIndex>
  // ir::OpSequences
  util::Set<ir::OperandIndex> external_operands;
  std::shared_ptr<custom::IKernelBuilder> custom_kernel_builder;
  bool is_linear_executor;
};

class BackendContext
{
public:
  BackendContext(const Backend *backend, ContextData &&data,
                 std::shared_ptr<ITensorRegistry> tensor_registry = nullptr)
    : _backend{backend}, _data{std::move(data)}, tensor_registry{tensor_registry}
  {
  }

  virtual ~BackendContext() = default;

  const Backend *backend() const { return _backend; }
  const ir::Graph *graph() const { return _data.graph.get(); }
  const util::Set<ir::OperandIndex> &external_operands() const { return _data.external_operands; }

  virtual ITensorRegistry *genTensors(const std::vector<onert::ir::OpSequenceIndex> &,
                                      const ir::OpSequences &, const ir::LowerInfoMap &)
  {
    return nullptr;
  }
  virtual FunctionMap genKernels(const std::vector<onert::ir::OpSequenceIndex> &,
                                 const ir::OpSequences &)
  {
    return {};
  }

protected:
  const Backend *_backend{nullptr};
  ContextData _data;

public:
  std::shared_ptr<ITensorRegistry> tensor_registry;
};

using BackendContexts = std::unordered_map<const Backend *, std::unique_ptr<BackendContext>>;

} // namespace backend
} // namespace onert

#endif // __ONERT_BACKEND_BACKEND_CONTEXT_H__
