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
#ifndef TVM_RUNTIME_PIPELINE_PIPELINE_FUNCTION_H_
#define TVM_RUNTIME_PIPELINE_PIPELINE_FUNCTION_H_
#include <tvm/runtime/module.h>
#include <tvm/runtime/packed_func.h>
#include <tvm/runtime/registry.h>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "pipeline_struct.h"
namespace tvm {
namespace runtime {
/*!
 * \brief The class that executes pipeline logic is used to initialize the thread pool,
    execute and schedule pipeline tasks, allocate and manage memory, etc.
 */
class PipelineFunction {
 public:
  /*!
   * \brief There are two mode to create graph runtime list, first is to use modules that
   *  are the module list already created by caller, when modules is empty these information
   *  from mod_configure will get use to create graph runtime list.
   * \param modules List of graph runtime module.
   * \param mod_configure Configure information that generate by export library function call.
   */
  std::vector<Module> PipelineCreateGraphruntime(Array<Module> modules,
                                                 const ModuleConfigure& mod_configure);
  /*!
   * \brief Initialize pipeline.
   * \param modules  List of graph runtime module.
   * \param pipeline_conf Dependency relation of each graph runtime module.
   * \param mod_configure Configure information that generate by export library function call.
   */
  size_t PipelineInit(Array<Module> modules, const PipelineConfigure& pipeline_conf,
                      const ModuleConfigure& mod_configure);
};
}  // namespace runtime
}  // namespace tvm
#endif  // TVM_RUNTIME_PIPELINE_PIPELINE_FUNCTION_H_
