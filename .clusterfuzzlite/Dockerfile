FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:4d95d77396ce573bbfca794166cd2f1e28df5b3445a45132e52ef0e4ff7f566b
RUN apt-get update && apt-get install -y cmake ninja-build
COPY . $SRC/clearCore
COPY .clusterfuzzlite/build.sh $SRC/
WORKDIR $SRC/clearCore
