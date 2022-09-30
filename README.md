# ISMRMRD/MRD Stream Reconstruction

This repository contains examples and demos of [ISMRMRD/MRD](https://github.com/ismrmrd/ismrmrd) reconstructions that use stdin and stdout as input and outputs. The examples introduce a workflow where MRD HDF5 files are converted inline to binary streams that are then consumed by reconstruction applications that emit images as a binary stream. The output stream can be written to an HDF5 file using an adapter. The workflow looks like:

```
HDF5 file (raw data) --> Adapter --(stream)--> Reconstruction --(stream)--> Adapter --> HDF5 file (images)
```

## Working with this repo
This repo contains a `.devcontainer` definition. For the best experience open the repo in VS Code and reopen in container. In the devcontainer, run CMake configure, build and install. You can also create a conda environment with the required dependencies using the [environment.yml](environment.yml) file. If you wish to test docker containers, you can build C++ and Python reconstruction containers using the [build_images.sh](build_images.sh) script.

## Working with streaming reconstruction

After building and installing the executables in the repo, you should be able to perform reconstructions like this:

```bash
# First generate some data
ismrmrd_generate_cartesian_shepp_logan

# Perform reconstruction
mrdhdf5_to_stream testdata.h5 | mrd_stream_recon | stream_to_mrdhdf5 images.h5
```

You should be able to view the reconstructed image (phantom) in a Jupyter notebook:

```python
import h5py
import numpy as np
from matplotlib import pyplot as plt

h = h5py.File('/workspaces/mrd_stream/images.h5')
pix = np.array(h[list(h.keys())[0]]['image_0']['data']).squeeze()
plt.imshow(pix)
```

The code for the example reconstruction can be found in [cpp/mrd_stream_recon.cpp](cpp/mrd_stream_recon.cpp).

## Running Python reconstructions

Python scripts can also be used as streaming reconstructions. There is an example of such a reconstrucrion in [python/mrd_stream_recon.py](python/mrd_stream_recon.py). To run this reconstruction:

```bash
mrdhdf5_to_stream testdata.h5 | python python/mrd_stream_recon.py | stream_to_mrdhdf5 pythonimages.h5
```

## Using Docker

The streaming reconstructions can also be packed up on Docker images. Run `build_images.sh` to build docker images of the C++ and Python reconstructions in this repo and then run recon with:

```bash
mrdhdf5_to_stream testdata.h5 | docker run -i mrd_stream_cpp_recon | stream_to_mrdhdf5 dockercppimages.h5
```

or

```bash
mrdhdf5_to_stream testdata.h5 | docker run -i mrd_stream_python_recon | stream_to_mrdhdf5 dockerpythonimages.h5
```

The latest versions of the Gadgetron also support streaming reconstruction, e.g.:

```bash
mrdhdf5_to_stream testdata.h5 | docker run -i ghcr.io/gadgetron/gadgetron/gadgetron_ubuntu_rt_nocuda:latest --from_stream -c default.xml | stream_to_mrdhdf5 dockergtimages.h5
```

## Remote reconstruction

Tools like the [Gadgetron](https://github.com/gadgetron/gadgetron) have traditionally used sockets to send the imaging data and have used the same socket for configuring the reconstruction. In the streaming reconstruction we use stdin and stdout exclusively for data transmission, but that doesn't prevent you from transmitting the data over the network if your reconstruction should run on a remote system. To enable this you can use a socket relay such as [socat](https://linux.die.net/man/1/socat).

On the server side, you would run:

```bash
socat TCP4-LISTEN:9222 "EXEC:mrd_stream_recon"
```

and on the client side:

```bash
mrdhdf5_to_stream testdata.h5 | socat -t30 - TCP4:server.mydomain.com:9222 | stream_to_mrdhdf5 socketimages.h5
```

The `-t30` switch tells socat to wait 30 seconds from when the input stream closes to when it should close the output stream if it is not closed yet. Since we will finish sending data before we get all the images back, we need a longer timeout than the standard 0.5s.

Often, you would want to establish the connection with the remote server using something like SSH. This workflow is relatively easily achieved. For example if we want to run the docker image `mrd_stream_python_recon` on the remote server named `my-remote-server` (where you have SSH access), you would run:

```bash
ssh -L 9002:localhost:9223 my-remote-server socat TCP4-LISTEN:9223 \"EXEC:docker run -i mrd_stream_python_recon\"
```

This will ask socat (on the remote system) to listen on port 9223 and forward to a docker container when a connection comes in, but we will also forward our local port 9002 to port 9223 on the remote system. We can now run the recon with:

```bash
mrdhdf5_to_stream -i testdata.h5 | socat -t30 - TCP:localhost:9002 | mrdhdf5_to_stream -o remoteimages.h5
```
