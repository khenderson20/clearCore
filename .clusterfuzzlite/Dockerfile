FROM gcr.io/oss-fuzz-base/base-builder:v1
RUN apt-get update && apt-get install -y cmake ninja-build
COPY . $SRC/clearCore
COPY .clusterfuzzlite/build.sh $SRC/
WORKDIR $SRC/clearCore
