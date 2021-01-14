/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "ExecutorFactory.h"

#include <deque>
#include <functional>
#include "ir/OperationCloner.h"
#include "exec/ExecutionObservers.h"
#include "exec/LinearExecutor.h"
#include "exec/DataflowExecutor.h"
#include "exec/ParallelExecutor.h"
#include "compiler/BackendManager.h"
#include "compiler/ExecutionBuilder.h"
#include "exec/ExecTime.h"
#include "compiler/Linear.h"
#include "compiler/BackendManager.h"
#include "backend/IPortableTensor.h"
#include "backend/builtin/Config.h"
#include "backend/builtin/KernelGenerator.h"
#include "backend/builtin/UserTensor.h"
#include "backend/builtin/TensorBuilder.h"
#include "util/TracingCtx.h"

#include <memory>

namespace onert
{
namespace
{

class SyncFunction final : public exec::IFunction
{
public:
  virtual ~SyncFunction() = default;
  SyncFunction(std::unique_ptr<exec::IFunction> fn, const std::shared_ptr<backend::IConfig> config)
    : _fn{std::move(fn)}, _config{config}
  {
    assert(_fn);
    assert(_config);
  }

  void run() override
  {
    _fn->run();
    _config->sync();
  }

  void prepare() override { _fn->prepare(); }

private:
  std::unique_ptr<exec::IFunction> _fn;
  std::shared_ptr<backend::IConfig> _config;
};

void initializeSubgraphIOTensors(compiler::LoweredGraph &lowered_graph,
                                 const backend::BackendContexts &backend_contexts,
                                 const ir::OperandIndexSequence &indices)
{
  // TODO Store builtin backend in BackendContext
  std::shared_ptr<backend::builtin::TensorRegistry> builtin_tensor_reg;
  for (const auto &e : backend_contexts)
  {
    auto backend = e.first;
    auto &context = e.second;
    if (backend->config()->id() == backend::builtin::Config::ID)
    {
      builtin_tensor_reg =
        std::dynamic_pointer_cast<backend::builtin::TensorRegistry>(context->tensor_registry);
    }
  }
  assert(builtin_tensor_reg);

  for (auto ind : indices)
  {
    const auto &operand = lowered_graph.graph().operands().at(ind);
    auto tensor = std::make_unique<backend::builtin::IOTensor>(
      operand.info(),
      ir::Layout::NHWC /* FIXME find operation for this operand and use frontend_layout */
    );

    // Add tensor to builtin TensorRegistry.
    builtin_tensor_reg->setNativeIOTensor(ind, std::move(tensor));
  }
}

backend::BackendContexts createBackendContexts(const compiler::LoweredGraph &lgraph,
                                               bool linear_executor)
{
  backend::BackendContexts contexts;
  auto &backend_manager = compiler::BackendManager::get();

  std::unordered_map<const backend::Backend *, backend::ContextData> context_data_map;

  for (auto backend : backend_manager.getAll())
  {
    auto &data = context_data_map[backend];
    auto graph = std::make_unique<ir::Graph>();
    graph->setLayout(lgraph.graph().layout());
    data.graph = std::move(graph);
  }

  // Generate partial graphs for each backend
  ir::OperationCloner op_cloner;
  auto &whole_graph = lgraph.graph();
  whole_graph.operations().iterate(
    [&](const ir::OperationIndex &op_ind, const ir::Operation &operation) {
      auto &op_li = lgraph.lower_info().operation;
      auto &operand_li = lgraph.lower_info().operand;
      auto backend = op_li.at(op_ind).backend();
      auto &partial_graph = *context_data_map[backend].graph;
      auto &operation_layouts = context_data_map[backend].operation_layouts;
      auto &operand_layouts = context_data_map[backend].operand_layouts;
      auto &external_operands = context_data_map[backend].external_operands;

      {
        auto io_list = (operation.getInputs() + operation.getOutputs()) | ir::Remove::DUPLICATED |
                       ir::Remove::UNDEFINED;
        for (auto operand_ind : io_list)
        {
          const auto &operand = whole_graph.operands().at(operand_ind);
          auto new_operand = std::make_unique<ir::Operand>(operand);
          // TODO Introduce a method for resetting use/def values of Operand
          const_cast<ir::OperationIndexSet &>(new_operand->getUses()).clear();
          new_operand->unsetDef();
          auto new_operand_ind = partial_graph.addOperand(operand_ind, std::move(new_operand));
          assert(!new_operand_ind.valid() || new_operand_ind == operand_ind);
          // If it failed, it means that the operand was added already
          if (new_operand_ind.valid())
          {
            // Add entries for external_operands and operand_layouts
            const auto &permute_factor = operand_li.at(operand_ind).def_factors().getOnlyElement();
            if (permute_factor.backend() != backend)
            {
              VERBOSE(BuildBackendGraph) << "backend:" << backend->config()->id()
                                         << " Added External Operand " << operand_ind << std::endl;
              external_operands.add(operand_ind);
            }
            operand_layouts[operand_ind] = permute_factor.layout();

            // Make the in/outputs be in/outputs in the partial graph
            if (whole_graph.getInputs().contains(operand_ind))
              partial_graph.addInput(operand_ind);
            if (whole_graph.getOutputs().contains(operand_ind))
              partial_graph.addOutput(operand_ind);
            VERBOSE(BuildBackendGraph) << "backend:" << backend->config()->id()
                                       << " Adding Operand " << operand_ind << std::endl;
          }
        }

        operation.accept(op_cloner);
        auto new_op_ind = partial_graph.addOperation(op_ind, op_cloner.releaseClone());
        operation_layouts[new_op_ind] = op_li.at(new_op_ind).layout();
        assert(new_op_ind == op_ind);
        VERBOSE(BuildBackendGraph) << "backend:" << backend->config()->id() << " Added Operation "
                                   << new_op_ind << std::endl;
      }
    });

  // Create contexts
  auto whole_op_order = lgraph.graph().topolSortOperations();
  for (auto &pair : context_data_map)
  {
    auto backend = pair.first;
    auto &data = pair.second;
    data.graph->finishBuilding();
    std::copy_if(whole_op_order.begin(), whole_op_order.end(), std::back_inserter(data.op_order),
                 [&](const auto &ind) { return data.graph->operations().exist(ind); });
    data.is_linear_executor = linear_executor;
    data.custom_kernel_builder = lgraph.graph().getKernelBuilder();
    contexts.emplace(backend, backend->newContext(std::move(data)));
  }
  return contexts;
}

} // namespace
} // namespace onert

namespace onert
{
namespace compiler
{

ExecutorFactory &ExecutorFactory::get()
{
  static ExecutorFactory singleton;
  return singleton;
}

ExecutorFactory::ExecutorFactory()
{
  _map["Linear"] = createLinearExecutor;
  _map["Dataflow"] = std::bind(createDataflowExecutor, std::placeholders::_1, std::placeholders::_2,
                               std::placeholders::_3, false);
  _map["Parallel"] = std::bind(createDataflowExecutor, std::placeholders::_1, std::placeholders::_2,
                               std::placeholders::_3, true);
}

exec::IExecutor *ExecutorFactory::create(std::unique_ptr<compiler::LoweredGraph> lowered_graph,
                                         const compiler::CompilerOptions &options,
                                         const std::shared_ptr<exec::ExecutorMap> &executor_map)
{
  return _map.at(options.executor)(std::move(lowered_graph), options, executor_map);
}

void ExecutorFactory::prepareMigrantTensors(compiler::LoweredGraph &lowered_graph,
                                            const backend::BackendContexts &backend_contexts)
{
  TensorRegistries tensor_regs{backend_contexts, true};

  lowered_graph.graph().operations().iterate(
    [&](const ir::OperationIndex &op_ind, const ir::Operation &op) {
      auto lower_info = lowered_graph.lower_info().operation.getRawPtr(op_ind);
      auto &backend_ctx = backend_contexts.at(lower_info->backend());
      for (auto ind :
           (op.getInputs() + op.getOutputs()) | ir::Remove::DUPLICATED | ir::Remove::UNDEFINED)
      {
        // If an Operation's input/output tensor does not have an own tensor object,
        // it must be using migrant tensors, so find the tensor from other tensor registries and
        // register it to the current tensor registry if it is portable
        if (!backend_ctx->tensor_registry->getITensor(ind))
        {
          auto tensor = tensor_regs.getITensor(ind);
          assert(tensor); // The tensor must have been registered
          auto ptensor = dynamic_cast<backend::IPortableTensor *>(tensor);
          if (ptensor)
            backend_ctx->tensor_registry->setMigrantTensor(ind, ptensor);
        }
      }
    });
}

exec::IExecutor *
ExecutorFactory::createLinearExecutor(std::unique_ptr<compiler::LoweredGraph> lowered_graph,
                                      const compiler::CompilerOptions &options,
                                      const std::shared_ptr<exec::ExecutorMap> &executor_map)
{
  backend::BackendContexts backend_contexts =
    createBackendContexts(*lowered_graph, options.executor == "Linear");

  TensorRegistries tensor_regs{backend_contexts, true};

  assert(!lowered_graph->graph().isBuildingPhase());

  initializeSubgraphIOTensors(
    *lowered_graph, backend_contexts,
    (lowered_graph->graph().getInputs() + lowered_graph->graph().getOutputs()) |
      ir::Remove::DUPLICATED | ir::Remove::UNDEFINED);

  // linearize
  auto order = Linear::linearize(*lowered_graph);
  Linear::dump(*lowered_graph, order);

  for (auto &pair : backend_contexts)
  {
    pair.second->genTensors();
  }

  prepareMigrantTensors(*lowered_graph, backend_contexts);

  // Give some runtime objects to builtin KernelGenerator
  for (auto &pair : backend_contexts)
  {
    auto builtin_context = dynamic_cast<backend::builtin::BackendContext *>(pair.second.get());
    if (builtin_context != nullptr)
    {
      auto builtin_kernel_gen = builtin_context->kernel_gen;
      builtin_kernel_gen->setTensorRegistries(tensor_regs);
      builtin_kernel_gen->setExecutorMap(executor_map);
    }
  }

  ExecutionBuilder builder;

  // Adjust the order of backends for the upcoming iteration
  std::deque<std::pair<const backend::Backend *, backend::BackendContext *>> ordered_contexts;
  for (auto &pair : backend_contexts)
  {
    // NOTE builtin backend must be processed lastly.
    // This is because of Permute layer's specialty which is the only operation that could have
    // different ITensor objects for the input and the output. And it requires all other backends'
    // tensors are ready to use.
    if (pair.first->config()->id() == "builtin")
      ordered_contexts.emplace_back(pair.first, pair.second.get());
    else
      ordered_contexts.emplace_front(pair.first, pair.second.get());
  }

  // Generate kernels
  for (auto &pair : ordered_contexts)
  {
    auto codes = pair.second->genKernels();
    for (auto &pair : codes)
    {
      auto &op_ind = pair.first;
      auto &fn_seq = pair.second;
      auto &op = lowered_graph->graph().operations().at(op_ind);
      auto lower_info = lowered_graph->lower_info().operation.getRawPtr(op_ind);
      if (options.he_profiling_mode)
        fn_seq->wrap<SyncFunction>(lower_info->backend()->config());
      builder.append(op_ind, {op_ind, &op, lower_info, std::move(fn_seq)});
    }
  }

  auto code_map = builder.releaseCodeMap();

  auto exec = new exec::LinearExecutor{
    std::move(lowered_graph), std::move(backend_contexts), tensor_regs, std::move(code_map), order,
    options.tracing_ctx};

  if (!options.trace_filepath.empty())
  {
    std::unique_ptr<exec::IExecutionObserver> ctp = std::make_unique<exec::TracingObserver>(
      options.trace_filepath, exec->graph(), options.tracing_ctx);
    exec->addObserver(std::move(ctp));
  }

  return exec;
}

exec::IExecutor *ExecutorFactory::createDataflowExecutor(
  std::unique_ptr<compiler::LoweredGraph> lowered_graph, const compiler::CompilerOptions &options,
  const std::shared_ptr<exec::ExecutorMap> &executor_map, bool parallel)
{
  backend::BackendContexts backend_contexts =
    createBackendContexts(*lowered_graph, options.executor == "Linear");

  TensorRegistries tensor_regs{backend_contexts, true};

  assert(!lowered_graph->graph().isBuildingPhase());

  initializeSubgraphIOTensors(
    *lowered_graph, backend_contexts,
    (lowered_graph->graph().getInputs() + lowered_graph->graph().getOutputs()) |
      ir::Remove::DUPLICATED | ir::Remove::UNDEFINED);

  for (auto &pair : backend_contexts)
  {
    pair.second->genTensors();
  }

  prepareMigrantTensors(*lowered_graph, backend_contexts);

  // Give some runtime objects to builtin KernelGenerator
  for (auto &pair : backend_contexts)
  {
    auto builtin_context = dynamic_cast<backend::builtin::BackendContext *>(pair.second.get());
    if (builtin_context != nullptr)
    {
      auto builtin_kernel_gen = builtin_context->kernel_gen;
      builtin_kernel_gen->setTensorRegistries(tensor_regs);
      builtin_kernel_gen->setExecutorMap(executor_map);
    }
  }

  ExecutionBuilder builder;

  // Adjust the order of backends for the upcoming iteration
  std::deque<std::pair<const backend::Backend *, backend::BackendContext *>> ordered_contexts;
  for (auto &pair : backend_contexts)
  {
    // NOTE builtin backend must be processed lastly.
    // This is because of Permute layer's specialty which is the only operation that could have
    // different ITensor objects for the input and the output. And it requires all other backends'
    // tensors are ready to use.
    if (pair.first->config()->id() == "builtin")
      ordered_contexts.emplace_back(pair.first, pair.second.get());
    else
      ordered_contexts.emplace_front(pair.first, pair.second.get());
  }

  // Generate kernels
  for (auto &pair : ordered_contexts)
  {
    auto codes = pair.second->genKernels();
    for (auto &pair : codes)
    {
      auto &op_ind = pair.first;
      auto &fn_seq = pair.second;
      auto &op = lowered_graph->graph().operations().at(op_ind);
      auto lower_info = lowered_graph->lower_info().operation.getRawPtr(op_ind);
      if (options.he_profiling_mode)
        fn_seq->wrap<SyncFunction>(lower_info->backend()->config());
      builder.append(op_ind, {op_ind, &op, lower_info, std::move(fn_seq)});
    }
  }

  auto code_map = builder.releaseCodeMap();

  exec::ExecutorBase *exec = nullptr;
  if (parallel)
  {
    exec = new exec::ParallelExecutor{std::move(lowered_graph), std::move(backend_contexts),
                                      tensor_regs, std::move(code_map), options.tracing_ctx};
  }
  else
  {
    auto dataflow_exec =
      new exec::DataflowExecutor{std::move(lowered_graph), std::move(backend_contexts), tensor_regs,
                                 std::move(code_map), options.tracing_ctx};
    if (options.he_profiling_mode)
    {
      std::vector<const backend::Backend *> backends;
      for (const auto &pair : backend_contexts)
      {
        backends.push_back(pair.first);
      }
      auto et = std::make_shared<exec::ExecTime>(backends);
      std::unique_ptr<exec::IExecutionObserver> obs =
        std::make_unique<exec::ProfileObserver>(et, dataflow_exec->graph());
      dataflow_exec->addObserver(std::move(obs));
    }
    exec = dataflow_exec;
  }

  if (!options.trace_filepath.empty())
  {
    std::unique_ptr<exec::IExecutionObserver> ctp = std::make_unique<exec::TracingObserver>(
      options.trace_filepath, exec->graph(), options.tracing_ctx);
    exec->addObserver(std::move(ctp));
  }

  return exec;
}

} // namespace compiler
} // namespace onert
