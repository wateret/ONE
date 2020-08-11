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

#ifndef __ONERT_BACKEND_ITENSOR_REGISTRY__
#define __ONERT_BACKEND_ITENSOR_REGISTRY__

#include <memory>

#include "ir/Index.h"
#include "backend/ITensor.h"

namespace onert
{
namespace backend
{

struct ITensorRegistry
{
  /**
   * @brief Deconstruct itself
   */
  virtual ~ITensorRegistry() = default;

  /**
   * @brief Returns pointer of ITensor among native and migrant tensors
   *
   * Native Tensor is a tensor that is managed by this backend
   * Migrant Tensor is a tensor that is imported from another backend
   *
   * @note  Return tensor cannot be used longer than dynamic tensor manager
   */
  virtual std::shared_ptr<ITensor> getITensor(const ir::OperandIndex &) = 0;
  /**
   * @brief Returns pointer of ITensor among native tensors
   *
   * Unlike @c getITensor , this function only searches from native tensors
   *
   * @note  Returned tensor cannot be used longer than dynamic tensor manager
   */
  virtual std::shared_ptr<ITensor> getNativeITensor(const ir::OperandIndex &) = 0;
};

} // namespace backend
} // namespace onert

#include "ir/OperandIndexMap.h"
#include "backend/IPortableTensor.h"

namespace onert
{
namespace backend
{

/**
 * @brief  TensorRegistry template class for the convenience of backend implementations
 *
 * If a backend uses @c IPortableTensor , and there is no special reason to implement @c
 * ITensorRegistry on your own, you may just use this default implementation.
 *
 * @tparam T_Tensor Tensor type. Must be a subclass of @c onert::backend::IPortableTensor .
 */
template <typename T_Tensor> class PortableTensorRegistryTemplate : public ITensorRegistry
{
public:
  std::shared_ptr<ITensor> getITensor(const ir::OperandIndex &ind) override
  {
    static_assert(std::is_base_of<ITensor, T_Tensor>::value, "T_Tensor must derive from ITensor.");
    auto external_tensor = _migrant.find(ind);
    if (external_tensor != _migrant.end())
      return external_tensor->second;
    return getNativeTensor(ind);
  }

  std::shared_ptr<ITensor> getNativeITensor(const ir::OperandIndex &ind) override
  {
    return getNativeTensor(ind);
  }

  std::shared_ptr<IPortableTensor> getPortableTensor(const ir::OperandIndex &ind)
  {
    auto external_tensor = _migrant.find(ind);
    if (external_tensor != _migrant.end())
    {
      if (external_tensor->second)
        return external_tensor->second;
    }
    return getNativeTensor(ind);
  }

  std::shared_ptr<T_Tensor> getNativeTensor(const ir::OperandIndex &ind)
  {
    auto tensor = _native.find(ind);
    if (tensor != _native.end())
      return tensor->second;
    return nullptr;
  }

  bool setMigrantTensor(const ir::OperandIndex &ind, const std::shared_ptr<IPortableTensor> &tensor)
  {
    assert(tensor != nullptr);
    auto itr = _native.find(ind);
    if (itr != _native.end())
      throw std::runtime_error{"Tried to set a migrant tensor but a native tensor already exists."};
    _migrant[ind] = tensor;
    return true;
  }

  void setNativeTensor(const ir::OperandIndex &ind, const std::shared_ptr<T_Tensor> &tensor)
  {
    assert(tensor != nullptr);
    auto itr = _migrant.find(ind);
    if (itr != _migrant.end())
      throw std::runtime_error{"Tried to set a native tensor but a migrant tensor already exists."};
    _native[ind] = tensor;
  }

  const ir::OperandIndexMap<std::shared_ptr<T_Tensor>> &native_tensors() { return _native; }

  const ir::OperandIndexMap<std::shared_ptr<IPortableTensor>> &migrant_tensors()
  {
    return _migrant;
  }

private:
  ir::OperandIndexMap<std::shared_ptr<IPortableTensor>> _migrant;
  ir::OperandIndexMap<std::shared_ptr<T_Tensor>> _native;
};

} // namespace backend
} // namespace onert

#endif // __ONERT_BACKEND_ITENSOR_REGISTRY__
