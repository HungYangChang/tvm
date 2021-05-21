# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

import numpy as np
import tvm
import tvm.testing
from tvm import relay
from tvm.relay import transform
from tvm.contrib import graph_executor, pipeline_executor


def run_modules(mod_configs, dev, target, dname, data):
    mod_input = {}
    final_output = {}
    indx = 1
    for mod in mod_configs:
        with tvm.transform.PassContext(opt_level=3):
            lib = relay.build(mod, target)

        m = graph_executor.GraphModule(lib["default"](dev))
        # Get input information
        mod_key = indx
        if mod_key in mod_input:
            for input in mod_input[mod_key]:
                input = mod_input[mod_key][input]
                m.set_input("data_{}".format(input["index"] - 1), input["data"])
        else:
            m.set_input(dname, data)
        m.run()
        n = m.get_num_outputs()
        # parse mod_config and set current output as next mod input data
        mconfig = mod_configs[mod]
        for output in mconfig["pipeline"]["output"]:
            #output = mconfig["pipeline"]["output"][output]
            output_data = m.get_output(output["output_indx"] - 1).asnumpy()
            for dep in output["dependent"]:
                # currnet output use as dependent input,
                # input_indx indicate the input index number.
                input_indx = dep["input_indx"]
                mod_indx = dep["mod_indx"]
                if mod_indx == 0:
                    final_output[input_indx] = output_data
                else:
                    if mod_indx in mod_input:
                        mod_input[mod_indx][input_indx] = {"index":input_indx, "data":output_data}
                    else:
                        mod_input[mod_indx] = {input_indx:{"index":input_indx, "data":output_data}}
        #output = m.get_output(0).asnumpy()
        #data = output

        indx = indx + 1

    return final_output


def get_mannual_mod():
    mods = []
    dshape = (3, 3)
    data = relay.var("data", relay.TensorType(dshape, "float32"))
    data_net1_output_1 = relay.var("data_0", relay.TensorType(dshape, "float32"))
    data_net1_output_2 = relay.var("data_1", relay.TensorType(dshape, "float32"))
    data_net2_output_1 = relay.var("data_0", relay.TensorType(dshape, "float32"))
    mvalue1 = np.full((1), 1).astype("float32")
    mvalue2 = np.full((1), 2).astype("float32")
    mvalue3 = np.full((1), 3).astype("float32")
    mv1 = relay.Constant(tvm.nd.array(mvalue1))
    mv2 = relay.Constant(tvm.nd.array(mvalue2))
    mv3 = relay.Constant(tvm.nd.array(mvalue3))

    # net1 have three output, output3 is final output
    net_output1 = relay.add(data, mv1)
    net_output2 = relay.subtract(data, mv2)
    net_output3 = relay.multiply(data, mv3)

    # net2 use net1 output1 as input
    net2 = relay.add(data_net1_output_1, mv2)
    net2 = relay.add(net2, mv3)

    # net3 use net2 output1 and net1 outpu2 as input
    net3 = relay.multiply(data_net2_output_1, mv3)
    net3 = relay.add(net3, data_net1_output_2)

    mods.append(tvm.IRModule.from_expr(relay.Function([data],
                                       relay.Tuple([net_output1,
                                                    net_output2,
                                                    net_output3])
        )))
    mods.append(tvm.IRModule.from_expr(relay.Function([data_net1_output_1],
                                                       net2)))
    mods.append(tvm.IRModule.from_expr(relay.Function([data_net1_output_2, data_net2_output_1],
                                                       net3)))

    return mods, dshape


def run_pipeline(target):
    """
    #Get 4 pipeline module.
    """
    mods, dshape = get_mannual_mod()
    """
    #Prepare batch data for pipeline feeding
    """
    datas = []
    for i in range(len(mods) + 1):
        datas.append(np.full(dshape, 3 + i).astype("float32"))

    # set configure
    indx = 0
    mod_config = {}
    mconfig = {"target_host": None, "mod_name": "default", "build": None, "params": None}
    mconfig1 = mconfig.copy()
    mconfig1["target"] = target[0]
    mconfig1["dev"] = target[1]
    # third output is final output, second output for mod2, third for  mod3
    # input
    mconfig1["pipeline"] = {
            "mod_indx":1,
            "output":[
             {"output_indx":1,
              "dependent":[{"mod_indx":2, "input_indx":1}]}, 
             {"output_indx":2,
              "dependent":[{"mod_indx":3, "input_indx":2}]},
             {"output_indx":3, 
              "dependent":[{"mod_indx":0, "input_indx":1}]},
             ]
             }
    mod_config[mods[0]] = mconfig1 

    mconfig2 = mconfig.copy()
    mconfig2["target"] = "llvm"
    mconfig2["dev"] = tvm.cpu(0)
    mconfig2["pipeline"] = {
            "mod_indx":2,
            "output":
            [{"output_indx":1,
              "dependent":[{"mod_indx":3, "input_indx":1}]},
            ]
                     }
    mod_config[mods[1]] = mconfig2

    mconfig3 = mconfig.copy()
    mconfig3["target"] = "llvm"
    mconfig3["dev"] = tvm.cpu(0)
    mconfig3["pipeline"] = {
            "mod_indx":3,
            "output":
             [{"output_indx":1,
               "dependent":[{"mod_indx":0, "input_indx":2}]}
             ]
                     }
    mod_config[mods[2]] = mconfig3

    """
    #Run with graph executor for verification purpose
    """
    outs = [run_modules(mod_config, tvm.cpu(), "llvm", "data", data) for data in datas]
    """


    #build and create pipeline module
    """
    with relay.build_config(opt_level=3):
        pipeline_module = pipeline_executor.create(mods, mod_config)

    """
    #Use pipeline executor to pipeline the said pipeline which use different backend
    """
    for data in datas:
        pipeline_module.set_input("data", data)
        pipeline_module.run()

    """
    Get result
    """
    pipeline_outputs = []
    for i in range(len(datas)):
        pipeline_outputs.append(pipeline_module.get_output()[0].asnumpy())

    """
    #Stop pipeline execution.
    """
    pipeline_module.stop()
    """

    #Verify result
    """
    for ref_out, out in zip(outs, pipeline_outputs):
        tvm.testing.assert_allclose(ref_out, out)


def test_pipeline():
    target_list = tvm.testing.enabled_targets()
    for target in target_list:
        run_pipeline(target)
        break


if __name__ == "__main__":
    test_pipeline()
