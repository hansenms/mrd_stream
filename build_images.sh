#!/bin/bash

docker build -t mrd_stream_python_recon --target pythonruntime .
docker build -t mrd_stream_cpp_recon --target cppruntime .
