FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:93e48e3853730255e873f9f3c63c90f3dfc298edd85fe3c02c0b9d399b87a80a
RUN apt-get update && apt-get install -y cmake ninja-build
COPY . $SRC/clearCore
COPY .clusterfuzzlite/build.sh $SRC/
WORKDIR $SRC/clearCore
