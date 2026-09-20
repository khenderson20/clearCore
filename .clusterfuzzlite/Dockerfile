FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:df8728a13b864d0db206ba6daee06fcdb812cf82111c41d54d8db4d6e585437e
RUN apt-get update && apt-get install -y cmake ninja-build
COPY . $SRC/clearCore
COPY .clusterfuzzlite/build.sh $SRC/
WORKDIR $SRC/clearCore
