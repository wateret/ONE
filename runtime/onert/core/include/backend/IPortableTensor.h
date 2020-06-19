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

#ifndef __ONERT_BACKEND_I_PORTABLE_TENSOR_H__
#define __ONERT_BACKEND_I_PORTABLE_TENSOR_H__

#include "backend/ITensor.h"

namespace onert
{
namespace backend
{

class IPortableTensor : public ITensor
{
public:
  virtual ~IPortableTensor() = default;

public:
  //virtual uint8_t *buffer() const = 0;
  //virtual size_t total_size() const = 0;
  //virtual size_t dimension(size_t index) const = 0;
  //virtual size_t num_dimensions() const = 0;
  //virtual size_t calcOffset(const ir::Coordinates &coords) const = 0;
  //virtual ir::Layout layout() const = 0;
  //virtual ir::DataType data_type() const = 0;
  //virtual float data_scale() const = 0;
  //virtual int32_t data_offset() const = 0;
  bool has_padding() const final { return false; }
  void access(const std::function<void(ITensor &tensor)> &fn) final { fn(*this); }
};

} // namespace backend
} // namespace onert

#endif // __ONERT_BACKEND_I_PORTABLE_TENSOR_H__
