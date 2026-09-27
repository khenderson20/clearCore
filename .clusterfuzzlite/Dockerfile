FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:23f294580c94a53c1625f5b98c7c22d7155fb030b877d0c2f937fbc8156b5aca
RUN apt-get update && apt-get install -y cmake ninja-build
COPY . $SRC/clearCore
COPY .clusterfuzzlite/build.sh $SRC/
WORKDIR $SRC/clearCore
