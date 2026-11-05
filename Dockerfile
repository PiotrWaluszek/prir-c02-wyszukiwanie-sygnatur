FROM ubuntu:24.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake make g++ libopenmpi-dev openmpi-bin python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /c02
COPY . .
RUN cmake -B build && cmake --build build -j
ENV OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1
CMD ["bash", "demo/demo.sh"]
