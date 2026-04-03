# Dockerfile for P25 Control Channel Logger
# Builds only the lean p25-cc-logger binary, not the full trunk-recorder.
#
# Build (run from trunk-recorder root):
#   docker build -f p25-cc-logger/Dockerfile -t p25-cc-logger .
#
# Run:
#   docker run --rm --device=/dev/bus/usb \
#     -v $(pwd)/p25-cc-logger:/app \
#     p25-cc-logger --config /app/config.json
#
# Pipe to jq:
#   docker run --rm --device=/dev/bus/usb \
#     -v $(pwd)/p25-cc-logger:/app \
#     p25-cc-logger --config /app/config.json 2>/dev/null | jq .
#
# Log to file:
#   docker run --rm --device=/dev/bus/usb \
#     -v $(pwd)/p25-cc-logger:/app \
#     p25-cc-logger --config /app/config.json >> /app/cc_log.ndjson

FROM ubuntu:24.04 AS builder

RUN apt-get update && \
  apt-get -y upgrade && \
  export DEBIAN_FRONTEND=noninteractive && \
  apt-get install --no-install-recommends -y \
    build-essential \
    ca-certificates \
    cmake \
    git \
    gnuradio-dev \
    gr-osmosdr \
    libosmosdr-dev \
    libboost-all-dev \
    libcurl4-openssl-dev \
    libgmp-dev \
    libhackrf-dev \
    liborc-0.4-dev \
    libpthread-stubs0-dev \
    librtlsdr-dev \
    libsndfile1-dev \
    libssl-dev \
    libuhd-dev \
    libusb-dev \
    libusb-1.0-0-dev \
    pkg-config && \
  rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

WORKDIR /src/build
RUN cmake .. && make -j$(nproc) p25-cc-logger && \
    mkdir -p /newroot/usr/local/bin && \
    cp p25-cc-logger /newroot/usr/local/bin/

# Stage 2: minimal runtime
FROM ubuntu:24.04

RUN apt-get update && \
    apt-get -y upgrade && \
    apt-get install --no-install-recommends -y \
      ca-certificates \
      libboost-log1.83.0 \
      libboost-chrono1.83.0t64 \
      libgnuradio-digital3.10.9t64 \
      libgnuradio-analog3.10.9t64 \
      libgnuradio-filter3.10.9t64 \
      libgnuradio-uhd3.10.9t64 \
      libgnuradio-osmosdr0.2.0t64 \
      librtlsdr2 && \
    rm -rf /var/lib/apt/lists/* && \
    rm -rf /usr/share/{doc,man,info}

COPY --from=builder /newroot /

# Fix GnuRadio log level
RUN mkdir -p /etc/gnuradio/conf.d/ && \
    echo 'log_level = info' >> /etc/gnuradio/conf.d/gnuradio-runtime.conf && \
    ldconfig

WORKDIR /app
ENV HOME=/tmp

ENTRYPOINT ["p25-cc-logger"]
CMD ["--config", "/app/config.json"]
