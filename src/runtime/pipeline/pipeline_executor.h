/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \brief Tiny graph runtime that can run graph
 *        containing only tvm PackedFunc.
 * \file graph_runtime.h
 */
#ifndef TVM_RUNTIME_PIPELINE_PIPELINE_EXECUTOR_H_
#define TVM_RUNTIME_PIPELINE_PIPELINE_EXECUTOR_H_
#include <memory>
#include <string>
#include <vector>

#include "pipeline_function.h"
#include "../file_utils.h"
using namespace std;
namespace tvm {
namespace runtime {

/*!
 * \brief pipeline runtime.
 *
 *  This runtime can be acccesibly in various language via
 *  TVM runtime PackedFunc API.
 */
class TVM_DLL SubGraphRuntime : public ModuleNode {
 public:
  ~SubGraphRuntime() {
    /* stop pipeline threads and release data in deconstructor.
     */
    Stop();
  }
  /*!
   * \brief Get member function to front-end
   * \param name The name of the function.
   * \param sptr_to_self The pointer to the module node.
   * \return The corresponding member function.
   */
  virtual PackedFunc GetFunction(const std::string& name, const ObjectPtr<Object>& sptr_to_self);

  /*!
   * \return The type key of the executor.
   */
  const char* type_key() const final { return "SubGraphRuntime"; }
  void Run();
  void Stop();
  void SetupStorage();

  /*!
   * \brief Initialize the graph executor with graph and context.
   * \param graph_json The execution graph.
   * \param module The module containing the compiled functions for the host
   *  processor.
   * \param ctxs The context of the host and devices where graph nodes will be
   *  executed on.
   * \param lookup_linked_param_func If given, a PackedFunc invoked to lookup linked parameters
   *  by storage_id. If not given, linked parameters are looked-up using an internal implementation,
   *  which is not compatible with RPCModules.
   */
  void Init(const Array<tvm::runtime::Module>& modules, const std::string& pipeline_json);

  /*!
   * \brief set index-th input to the graph.
   * \param index The input index.
   * \param data_in The input data.
   */
  void SetInput(int index, DLTensor* data_in);
  void SetInput(const std::string& name, DLTensor* data_in);
  NDArray GetInput(int index, int mIndx) const;
  NDArray GetInput(const std::string& name, int mIndx) const;
  /*!
   * \brief Get the number of outputs
   *
   * \return The number of outputs from graph.
   */
  int NumOutputs() const;
  /*!
   * \brief Get the number of inputs
   *
   * \return The number of inputs to the graph.
   */
  int NumInputs() const;
  /*!
   * \brief Return NDArray Array for all output.
   *
   * \param syncPoll Syncholization poll mode or ASyncholization.
   * \return NDArray Array for all output.
   */
  Array<NDArray> GetOutput(bool syncPoll = true);


  void Load(dmlc::JSONReader* reader) {
    reader->BeginArray();
    while (reader->NextArrayItem()) {
      std::string key;
      reader->BeginObject();
      int mod_indx = 0;
      unordered_map<int , unordered_map<int, int>> output;
      while (reader->NextObjectItem(&key)) {
        if (key == "mod_indx") {
          reader->Read(&mod_indx);
        }
        if (key == "output") {
          reader->BeginArray();
          while (reader->NextArrayItem()) {
            int output_indx = -1;
            unordered_map<int, int> depend;
            reader->BeginObject();
            while (reader->NextObjectItem(&key)) {
              if (key == "output_indx"){
                reader->Read(&output_indx);
              }
              if (key == "dependent") {
                reader->BeginArray();
                int dep_mod_indx = -1, input_indx = -1;
                while (reader->NextArrayItem()) {
                  reader->BeginObject();
                  while (reader->NextObjectItem(&key)) {
                    if (key == "mod_indx"){
                      reader->Read(&dep_mod_indx);
                    }
                    if (key == "input_indx") {
                      reader->Read(&input_indx);
                    }
                  }
                  if (dep_mod_indx >= 0 && input_indx >= 0) {
                    depend[dep_mod_indx] = input_indx;
                  }
              }
            }
          }

          if (output_indx >= 0 ) {
            output[output_indx] = depend;
          }
        }
      }
    }
    if (mod_indx >= 0) {
      pipeline_conf[mod_indx] = output;
    }
  }
}

 protected:
  vector<NDArray> output_entry_;
  PIPELINE_CONF pipeline_conf;
  vector<shared_ptr<RuntimeItem>> runtimes;
};
}  // namespace runtime
}  // namespace tvm

#endif  // TVM_RUNTIME_PIPELINE_PIPELINE_EXECUTOR_H_
