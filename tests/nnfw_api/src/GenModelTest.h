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

#include <gtest/gtest.h>
#include <nnfw_internal.h>

#include <fstream>
#include <string>
#include <unordered_map>

#include "CircleGen.h"
#include "fixtures.h"

inline size_t sizeOfNnfwType(NNFW_TYPE type)
{
  switch (type)
  {
    case NNFW_TYPE_TENSOR_BOOL:
    case NNFW_TYPE_TENSOR_UINT8:
    case NNFW_TYPE_TENSOR_QUANT8_ASYMM:
      return 1;
    case NNFW_TYPE_TENSOR_FLOAT32:
    case NNFW_TYPE_TENSOR_INT32:
      return 4;
    case NNFW_TYPE_TENSOR_INT64:
      return 8;
    default:
      throw std::runtime_error{"Invalid tensor type"};
  }
}

// TODO Unify this with `SessionObject` in `fixtures.h`
struct SessionObjectGeneric
{
  nnfw_session *session = nullptr;
  std::vector<std::vector<uint8_t>> inputs;
  std::vector<std::vector<uint8_t>> outputs;
};

struct TestCaseData
{
  /**
   * @brief A vector of input buffers
   */
  std::vector<std::vector<uint8_t>> inputs;

  /**
   * @brief A vector of output buffers
   */
  std::vector<std::vector<uint8_t>> outputs;

  /**
   * @brief Append vector data to inputs
   *
   * @tparam T Data type
   * @param data vector data array
   */
  template <typename T> void addInput(const std::vector<T> &data) { addData(inputs, data); }

  /**
   * @brief Append vector data to inputs
   *
   * @tparam T Data type
   * @param data vector data array
   */
  template <typename T> void addOutput(const std::vector<T> &data) { addData(outputs, data); }

  /**
   * @brief Set @c True if @c NNFW_STATUS_ERROR is expected after calling @c nnfw_run() with
   *        this test case; set @c False otherwise.
   */
  void expect_error_on_run(bool expect_error_on_run) { _expect_error_on_run = expect_error_on_run; }
  bool expect_error_on_run() const { return _expect_error_on_run; }

private:
  template <typename T>
  static void addData(std::vector<std::vector<uint8_t>> &dest, const std::vector<T> &data)
  {
    size_t size = data.size() * sizeof(T);
    dest.emplace_back();
    dest.back().resize(size);
    std::memcpy(dest.back().data(), data.data(), size);
  }

  bool _expect_error_on_run = false;
};

template <>
inline void TestCaseData::addData<bool>(std::vector<std::vector<uint8_t>> &dest,
                                        const std::vector<bool> &data)
{
  size_t size = data.size() * sizeof(uint8_t);
  dest.emplace_back();
  dest.back().resize(size);
  std::transform(data.cbegin(), data.cend(), dest.back().data(),
                 [](bool b) { return static_cast<uint8_t>(b); });
}

/**
 * @brief Create a TestCaseData with a uniform type
 *
 * A helper function for generating test cases that has the same data type for model inputs/outputs.
 *
 * @tparam T Uniform tensor type
 * @param inputs Inputs tensor buffers
 * @param outputs Output tensor buffers
 * @return TestCaseData Generated test case data
 */
template <typename T>
static TestCaseData uniformTCD(const std::vector<std::vector<T>> &inputs,
                               const std::vector<std::vector<T>> &outputs)
{
  TestCaseData ret;
  for (const auto &data : inputs)
    ret.addInput(data);
  for (const auto &data : outputs)
    ret.addOutput(data);
  return ret;
}

/**
 * @brief A test configuration class
 */
class GenModelTestContext
{
public:
  GenModelTestContext(CircleBuffer &&cbuf) : _cbuf{std::move(cbuf)}, _backends{"cpu"} {}

  /**
   * @brief  Return circle buffer
   *
   * @return CircleBuffer& the circle buffer
   */
  const CircleBuffer &cbuf() const { return _cbuf; }

  /**
   * @brief Return test cases
   *
   * @return std::vector<TestCaseData>& the test cases
   */
  const std::vector<TestCaseData> &test_cases() const { return _test_cases; }

  /**
   * @brief Return backends
   *
   * @return const std::vector<std::string>& the backends to be tested
   */
  const std::vector<std::string> &backends() const { return _backends; }

  /**
   * @brief Return test is defined to fail on model load
   *
   * @return bool test is defined to fail on model load
   */
  bool expected_expected_fail_model_load() const { return _expected_fail_model_load; }

  /**
   * @brief Return test is defined to fail on compile
   *
   * @return bool test is defined to fail on compile
   */
  bool expected_expected_fail_compile() const { return _expected_fail_compile; }

  /**
   * @brief Set the output buffer size of specified output tensor
   *        Note that output tensor size of a model with dynamic tensor is calculated while
   *        running the model.
   *        Therefore, before runniing the model, the sufficient size of buffer should
   *        be prepared by calling this method.
   *        The size does not need to be the exact size.
   */
  void output_sizes(uint32_t ind, size_t size) { _output_sizes[ind] = size; }

  size_t output_sizes(uint32_t ind) const { return _output_sizes.at(ind); }

  bool hasOutputSizes(uint32_t ind) const { return _output_sizes.find(ind) != _output_sizes.end(); }

  /**
   * @brief Add a test case
   *
   * @param tc the test case to be added
   */
  void addTestCase(const TestCaseData &tc) { _test_cases.emplace_back(tc); }

  /**
   * @brief Add a test case
   *
   * @param tc the test case to be added
   */
  void setBackends(const std::vector<std::string> &backends)
  {
    _backends.clear();

    for (auto backend : backends)
    {
#ifdef TEST_ACL_BACKEND
      if (backend == "acl_cl" || backend == "acl_neon")
      {
        _backends.push_back(backend);
      }
#endif
      if (backend == "cpu")
      {
        _backends.push_back(backend);
      }
    }
  }

  /**
   * @brief Expect failure while model load
   */
  void expectFailModelLoad() { _expected_fail_model_load = true; }

  /**
   * @brief Expect failure while compiling
   */
  void expectFailCompile() { _expected_fail_compile = true; }

private:
  CircleBuffer _cbuf;
  std::vector<TestCaseData> _test_cases;
  std::vector<std::string> _backends;
  std::unordered_map<uint32_t, size_t> _output_sizes;
  bool _expected_fail_model_load{false};
  bool _expected_fail_compile{false};
};

/**
 * @brief Generated Model test fixture for a one time inference
 *
 * This fixture is for one-time inference test with variety of generated models.
 * It is the test maker's responsiblity to create @c _context which contains
 * test body, which are generated circle buffer, model input data and output data and
 * backend list to be tested.
 * The rest(calling API functions for execution) is done by @c Setup and @c TearDown .
 *
 */
class GenModelTest : public ::testing::Test
{
protected:
  void SetUp() override
  { // DO NOTHING
  }

  void TearDown() override
  {
    for (std::string backend : _context->backends())
    {
      // NOTE If we can prepare many times for one model loading on same session,
      //      we can move nnfw_create_session to SetUp and
      //      nnfw_load_circle_from_buffer to outside forloop
      NNFW_ENSURE_SUCCESS(nnfw_create_session(&_so.session));
      auto &cbuf = _context->cbuf();
      auto model_load_result =
          nnfw_load_circle_from_buffer(_so.session, cbuf.buffer(), cbuf.size());
      if (_context->expected_expected_fail_model_load())
      {
        ASSERT_NE(model_load_result, NNFW_STATUS_NO_ERROR);
        std::cerr << "Failed model loading as expected." << std::endl;
        NNFW_ENSURE_SUCCESS(nnfw_close_session(_so.session));
        continue;
      }
      NNFW_ENSURE_SUCCESS(model_load_result);
      NNFW_ENSURE_SUCCESS(nnfw_set_available_backends(_so.session, backend.data()));

      if (_context->expected_expected_fail_compile())
      {
        ASSERT_EQ(nnfw_prepare(_so.session), NNFW_STATUS_ERROR);

        NNFW_ENSURE_SUCCESS(nnfw_close_session(_so.session));
        continue;
      }
      NNFW_ENSURE_SUCCESS(nnfw_prepare(_so.session));

      // In/Out buffer settings
      uint32_t num_inputs;
      NNFW_ENSURE_SUCCESS(nnfw_input_size(_so.session, &num_inputs));
      _so.inputs.resize(num_inputs);
      for (uint32_t ind = 0; ind < _so.inputs.size(); ind++)
      {
        nnfw_tensorinfo ti;
        NNFW_ENSURE_SUCCESS(nnfw_input_tensorinfo(_so.session, ind, &ti));
        uint64_t input_elements = num_elems(&ti);
        _so.inputs[ind].resize(input_elements * sizeOfNnfwType(ti.dtype));
        if (_so.inputs[ind].size() == 0)
        {
          // Optional inputs
          ASSERT_EQ(nnfw_set_input(_so.session, ind, ti.dtype, nullptr, 0), NNFW_STATUS_NO_ERROR);
        }
        else
        {
          ASSERT_EQ(nnfw_set_input(_so.session, ind, ti.dtype, _so.inputs[ind].data(),
                                   _so.inputs[ind].size()),
                    NNFW_STATUS_NO_ERROR);
        }
      }

      uint32_t num_outputs;
      NNFW_ENSURE_SUCCESS(nnfw_output_size(_so.session, &num_outputs));
      _so.outputs.resize(num_outputs);
      for (uint32_t ind = 0; ind < _so.outputs.size(); ind++)
      {
        nnfw_tensorinfo ti;
        NNFW_ENSURE_SUCCESS(nnfw_output_tensorinfo(_so.session, ind, &ti));

        auto size = 0;
        {
          if (_context->hasOutputSizes(ind))
          {
            size = _context->output_sizes(ind);
          }
          else
          {
            uint64_t output_elements = num_elems(&ti);
            size = output_elements * sizeOfNnfwType(ti.dtype);
          }
          _so.outputs[ind].resize(size);
        }

        ASSERT_GT(_so.outputs[ind].size(), 0) << "Please make sure TC output is non-empty.";
        ASSERT_EQ(nnfw_set_output(_so.session, ind, ti.dtype, _so.outputs[ind].data(),
                                  _so.outputs[ind].size()),
                  NNFW_STATUS_NO_ERROR);
      }

      // Set input values, run, and check output values
      for (auto &test_case : _context->test_cases())
      {
        auto &ref_inputs = test_case.inputs;
        auto &ref_outputs = test_case.outputs;
        ASSERT_EQ(_so.inputs.size(), ref_inputs.size());
        for (uint32_t i = 0; i < _so.inputs.size(); i++)
        {
          // Fill the values
          ASSERT_EQ(_so.inputs[i].size(), ref_inputs[i].size());
          memcpy(_so.inputs[i].data(), ref_inputs[i].data(), ref_inputs[i].size());
        }

        if (test_case.expect_error_on_run())
        {
          ASSERT_EQ(nnfw_run(_so.session), NNFW_STATUS_ERROR);
          continue;
        }

        NNFW_ENSURE_SUCCESS(nnfw_run(_so.session));

        ASSERT_EQ(_so.outputs.size(), ref_outputs.size());
        for (uint32_t i = 0; i < _so.outputs.size(); i++)
        {
          nnfw_tensorinfo ti;
          NNFW_ENSURE_SUCCESS(nnfw_output_tensorinfo(_so.session, i, &ti));

          // Check output tensor values
          auto &ref_output = ref_outputs[i];
          auto &output = _so.outputs[i];
          ASSERT_EQ(output.size(), ref_output.size());

          switch (ti.dtype)
          {
            case NNFW_TYPE_TENSOR_BOOL:
              // TODO Check if this comparison is correct
              compareBuffersExact<bool>(ref_output, output);
              break;
            case NNFW_TYPE_TENSOR_UINT8:
              compareBuffersExact<uint8_t>(ref_output, output);
              break;
            case NNFW_TYPE_TENSOR_INT32:
              compareBuffersExact<int32_t>(ref_output, output);
              break;
            case NNFW_TYPE_TENSOR_FLOAT32:
              // TODO better way for handling FP error?
              for (uint32_t e = 0; e < ref_output.size() / sizeof(float); e++)
              {
                float refval = reinterpret_cast<const float *>(ref_output.data())[e];
                float val = reinterpret_cast<const float *>(output.data())[e];
                EXPECT_NEAR(refval, val, 0.001) << "e == " << e;
              }
              break;
            case NNFW_TYPE_TENSOR_INT64:
              compareBuffersExact<int64_t>(ref_output, output);
              break;
            case NNFW_TYPE_TENSOR_QUANT8_ASYMM:
              throw std::runtime_error{"NYI : comparison of tensors of QUANT8_ASYMM"};
            default:
              throw std::runtime_error{"Invalid tensor type"};
          }
          // TODO Add shape comparison
        }
      }

      NNFW_ENSURE_SUCCESS(nnfw_close_session(_so.session));
    }
  }

private:
  template <typename T>
  void compareBuffersExact(const std::vector<uint8_t> &ref_buf, const std::vector<uint8_t> &act_buf)
  {
    for (uint32_t e = 0; e < ref_buf.size() / sizeof(T); e++)
    {
      float ref = reinterpret_cast<const T *>(ref_buf.data())[e];
      float act = reinterpret_cast<const T *>(act_buf.data())[e];
      EXPECT_EQ(ref, act) << "index == " << e;
    }
  }

protected:
  SessionObjectGeneric _so;
  std::unique_ptr<GenModelTestContext> _context;
};
