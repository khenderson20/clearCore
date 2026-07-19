FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:b9d9eae2a36dc27e2c853aaf754f90d3daaa2d8b2b1b7903699643e797a1af93
RUN apt-get update && apt-get install -y cmake ninja-build
COPY . $SRC/clearCore
COPY .clusterfuzzlite/build.sh $SRC/
WORKDIR $SRC/clearCore
