#!/bin/bash

set -eo pipefail

# enable conda for this shell
# shellcheck disable=SC1091
. /opt/conda/etc/profile.d/conda.sh

# activate the environment
conda activate mrd_stream

this_dir=$(dirname $0)
exec python "${this_dir}"/mrd_stream_recon.py
