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
#include "pipeline_function.h"

#include <utility>
using namespace tvm::runtime;

void pipeline_pipeline_run(const int& num, const shared_ptr<RuntimeItem>& curRunItem) {
  QUEUE* curQueue = curRunItem->queue;
  QUEUE* nextQueue = curRunItem->next->queue;

  /*
   * Wait at beginning, then only do wait once last time data poll failed,
   * the loop would break after an exit notification get received.
   */
  bool suc = false;
  while (curRunItem->waitPipeLineData(suc)) {
    suc = pipeline_queue_poll(curQueue, &curRunItem->rData);
    if (!suc) {
      continue;
    }

    curRunItem->Run();

    vector<shared_ptr<OutputData>> outputs;
    curRunItem->GetOutput2(&outputs);
    pipeline_queue_push(nextQueue, &outputs);
    curRunItem->notifyDataReadyToNext();
  }
  curRunItem->notifyNextExit();
}

thread* pipeline_pipeline_init(SHARED_RUNTIME_VEC* runtimes) {
  for (size_t i = 1; i < runtimes->size(); i++) {
    (*runtimes)[i]->t = thread(pipeline_pipeline_run, i, (*runtimes)[i]);
  }
  return NULL;
}

void pipeline_init(Array<Module> graphRuntimes,
                   SHARED_RUNTIME_VEC* runtimes,
                   PIPELINE_CONF* pipeline_conf) {
  int len = graphRuntimes.size();
  for (int i = 0; i < len; i++) {
    QUEUE* sub_queue = createQueue<SLOT>(NULL, SUB_Q_SIZE);
    auto runItem = make_shared<RuntimeItem>(graphRuntimes[i],
                                            sub_queue,
                                            &((*pipeline_conf)[i + 1]),
                                            i);
    runtimes->push_back(runItem);
    /*
       set prev and next for RuntimeItem, runtime need these information to
       poll data from prev and do notification for next.
       */
    if (i > 0) {
      (*runtimes)[i - 1]->next = (*runtimes)[i];
    }
    if (i == len - 1) {
      (*runtimes)[i]->next = (*runtimes)[0];
    }
  }
  pipeline_pipeline_init(runtimes);
  return;
}

//inline void pipeline_queue_push(QUEUE* queue, Array<NDArray> arrays) {
inline void pipeline_queue_push(QUEUE* queue, vector<shared_ptr<OutputData>>* outputs) {
  q_push<SLOT, vector<shared_ptr<OutputData>> *>(queue, outputs);
  return;
}

bool pipeline_queue_poll(QUEUE* queue, RuntimeData* runtimeData) {
  return q_poll<SLOT, RuntimeData>(queue, runtimeData);
}

/*
void subgraph_run_serial(const SHARED_RUNTIME_VEC runtimes) {
  runtimes[0]->Run();
  for (int i = 1; i < runtimes.size(); i++) {
    int oNum = runtimes[i - 1]->runtimePtr->NumOutputs();
    for (int j = 0; j < oNum; j++) {
      auto o = runtimes[i - 1]->runtimePtr->GetOutput(j);
      DLTensor* ptr = const_cast<DLTensor*>(o.operator->());
      runtimes[i]->runtimePtr->SetInput(j, ptr);
    }
    runtimes[i]->Run();
  }
}
*/

void pipeline_run(const SHARED_RUNTIME_VEC& runtimes) {
  //subgraph_run_serial(runtimes);
  //return;
  shared_ptr<RuntimeItem> runtime = runtimes.front();
  runtime->Run();

  vector<shared_ptr<OutputData>> outputs;
  runtime->GetOutput2(&outputs);
  pipeline_queue_push(runtime->next->queue, &outputs);
  runtime->notifyDataReadyToNext();
  return;
}

bool pipeline_poll(vector<NDArray>* output, const SHARED_RUNTIME_VEC& runtimes, const bool bSynch) {
  shared_ptr<RuntimeItem> firstRuntime = runtimes.front();
  QUEUE* queue = firstRuntime->queue;
  bool suc = false;
  pipelineOutputData<> outputData(output);
  suc = q_poll<SLOT, pipelineOutputData<>>(queue, &outputData);
  while (!suc && bSynch) {
    /*
     * If get exit notify then break.
     */
    if (!firstRuntime->waitPipeLineData(!bSynch)) {
      break;
    }
    suc = q_poll<SLOT, pipelineOutputData<>>(queue, &outputData);
  }
  return suc;
}

void pipeline_stop(const SHARED_RUNTIME_VEC& runtimes) { 
  if (!runtimes.empty()) {
    runtimes.front()->notifyNextExit(); 
  }
}
