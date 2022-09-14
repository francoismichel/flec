# FlEC

FlEC is a research paper redefining the loss recovery mechanism of the QUIC protocol through the use
of Forward Erasure Correction. In this article, we propose to adapt the loss recovery mechanism to the 
use-case using protocol plugins !

The full paper is part of the Transactions on Networking journal and can be found [here](https://ieeexplore.ieee.org/document/9861377) or on [arXiv](https://arxiv.org/abs/2208.07741).

All the code of FlEC is contained in the (not so simple!) `simple_fec` plugin under the `plugin` directory. This plugins also reguires the use of 
the `loss_monitor` plugin that gather network statistics concerning packet loss.

Three variations of FlEC are used in the article:
- `fec_bulk.plugin` for bulk downloads
- `fec_buffer_limited.plugin` for bulk downloads with limited receive windows
- `fec_real_time_messages.plugin` for the real-time messaging use-case

Note also that `ac_rlnc.plugin` implements the original AC-RLNC article.

## Running FlEC ni NS3-DCE and reproducing the results

While good results can be obtained in real-world networks such as Starlink when losses occur (c.f. the [FlEC article](https://ieeexplore.ieee.org/document/9861377)), the PQUIC implementation used by FlEC is computationnaly heavy and can deteriorate the 
data transfers in some cases. We thus provide here a full NS3-DCE working environment to evaluate FlEC and compare it with other solutions, being able to run *real* protocol implementations (not models) inside the NS3 environment.
Feel free to reuse it and add your own solutions in this environment !

We did our best to provide portable solutions and environment, so the FlEC implementation, the NS3-DCE environment and even the plotting scripts are provided through Docker containers ! We also ported NS3-DCE to a Ubuntu 20.04 docker image and this image can easily be used without FlEC to run your own solutions and projects.

The NS3-DCE environment is available [here](https://github.com/francoismichel/flec-simulation-experiments).

### Reproducing the results
The only dependency you need to install is git and [Docker](https://www.docker.com/).
The computations with NS3 are CPU-intensive, the more CPUs you have, the better it is. You will need between 2GB and 3GB of RAM per CPU you use for the experiments.

Here is a script you can run anywhere on Linux to reproduce the results of the FlEC article :

```bash
#! /usr/bin/env bash

git clone --recurse-submodules https://github.com/francoismichel/flec
git clone --recurse-submodules https://github.com/francoismichel/flec-simulation-experiments

pushd flec-simulation-experiments && bash reproduce_and_plot_results.sh ../flec && popd

echo Reproduction finished ! The resulting graphs can be found in flec-simulation-experiments/results_plots :
ls flec-simulation-experiments/results_plots

echo The raw results can be found in flec-simulation-experiments/results
```


# PQUIC

The PQUIC implementation, a framework that enables QUIC clients and servers to dynamically exchange protocol plugins that extend the protocol on a per-connection basis.

The current PQUIC implementation supports the draft-27 version of the QUIC specification.

# Building PQUIC

More detailed instructions are available at: https://pquic.org

PQUIC is developed in C, and is based on picoquic (https://github.com/private-octopus/picoquic).
It can be built under Linux (the support of Windows is not provided yet).
Building the project requires first managing the dependencies, Picotls, uBPF, michelfralloc, libarchive
and OpenSSL.

## PQUIC on Linux

To build PQUIC on Linux, you need to:

 * Install and build Openssl on your machine

 * Install libarchive. It is usually found in distribution packages (e.g., `apt install libarchive-dev`) or on (the LibArchive page)[http://libarchive.org/]

 * Clone and compile Picotls (https://github.com/p-quic/picotls), using cmake as explained in the Picotls documentation.

 * Clone and compile PQUIC with both its uBPF and michelfralloc dependencies:

~~~
   git submodule update --init
   cd ubpf/vm
   make
   cd ../..
   cd picoquic/michelfralloc
   make
   cd ../..
   cmake .
   make
~~~

## Documentation

Generate doc with
```bash
doxygen
```
