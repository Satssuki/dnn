# Deep Neural Nets

[![Build Status](https://travis-ci.org/liangfu/dnn.svg?branch=master)](https://travis-ci.org/liangfu/dnn)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

## Introduction

The Deep Neural Nets (DNN) library is a deep learning framework designed to be small in size, 
computationally efficient and portable.

We started the project as a fork of the popular [OpenCV](http://opencv.org/) library,
while removing some components that is not tightly related to the deep learning framework.
Comparing to Caffe and many other implements, DNN is relatively independent to third-party libraries, 
(Yes, we don't require Boost and Database systems to be install before crafting your own network models)
and it can be more easily portable to mobile systems, like iOS, Android and RaspberryPi etc.

## Available Modules

The following features have been implemented:

 - Mini-batch based learning, with OpenMP support
 - YAML based network definition
 - Gradient checking for all implemented layers

The following modules are implemented in current version:

 Module Name           | Description
 ---                   | ---
 `InputLayer`          | for storing original input images
 `ConvolutionLayer`    | performs 2d convolution upon images
 `MaxPoolingLayer`     | performs max-pooling operation
 `DenseLayer`          | fully connected Layer (with activation options, e.g. tanh, sigmoid, softmax, relu etc.)
 `SimpleRNNLayer`      | for processing sequence data
 `MergeLayer`          | for combining output results from multiple different layers

More modules will be available online !

### Network Definition

Layer Type | Attributes
--- | ---
`Input` | `name`,`n_input_planes`,`input_height`,`input_width`,`seq_length`
`Convolution` | `name`,`visualize`,`n_output_planes`,`ksize`
`MaxPooling` | `name`,`visualize`,`ksize`
`Dense` | `name`,`input_layer(optional)`,`visualize`,`n_output_planes`,`activation_type`
`SimpleRNN` | `name`,`n_output_planes`,`seq_length`,`time_index`,`activation_type`
`Merge` | `name`,`input_layers`,`visualize`,`n_output_planes`

With the above parameters given in YAML format, one can simply define a network. 
For instance, a lenet model can be:

```yaml
%YAML:1.0
layers:
  - {type: Input, name: input1, n_input_planes: 1, input_height: 28, input_width: 28, seq_length: 1}
  - {type: Convolution, name: conv1, visualize: 0, n_output_planes: 6, ksize: 5, stride: 1}
  - {type: MaxPooling, name: pool1, visualize: 0, ksize: 2, stride: 2}
  - {type: Convolution, name: conv2, visualize: 0, n_output_planes: 16, ksize: 5, stride: 1}
  - {type: MaxPooling, name: pool2, visualize: 0, ksize: 2, stride: 2}
  - {type: Dense, name: fc1, visualize: 0, n_output_planes: 10, activation_type: tanh}
```

Then, by ruuning network training program:

```bash
$ network train --solver data/mnist/lenet_solver.xml
```

one can start to train a simple network right away. And this is the way the source code 
and data models are tested in Travis-Ci. 
(See [.travis.yml](https://github.com/liangfu/dnn/blob/master/.travis.yml) in the root directory)

## Compilation

[CMake](https://cmake.org) is required for successfully compiling the project. 

Under root directory of the project:

 ```bash
 $ cd $DNN_ROOT
 $ mkdir build
 $ cmake .. 
 $ make -j4
 ```

## License

MIT

<script>
  (function(i,s,o,g,r,a,m){i['GoogleAnalyticsObject']=r;i[r]=i[r]||function(){
  (i[r].q=i[r].q||[]).push(arguments)},i[r].l=1*new Date();a=s.createElement(o),
  m=s.getElementsByTagName(o)[0];a.async=1;a.src=g;m.parentNode.insertBefore(a,m)
  })(window,document,'script','https://www.google-analytics.com/analytics.js','ga');
  ga('create', 'UA-8286931-2', 'auto');
  ga('send', 'pageview');
</script>
