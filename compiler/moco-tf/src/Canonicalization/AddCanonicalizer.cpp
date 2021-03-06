/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "AddCanonicalizer.h"

#include <moco/IR/TFDialect.h>
#include <moco/IR/TFNodes.h>

#include "TFEltwiseBinaryCanonicalzeHelper.h"

namespace moco
{
namespace tf
{

bool AddCanonicalizer::transform(TFAdd *node) const
{
  return canonicalize_eltwise_binary_node(node);
}

} // namespace tf
} // namespace moco
