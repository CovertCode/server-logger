FROM alpine:latest

# Install tools
RUN apk add --no-cache g++ make git

WORKDIR /app

# --- FIX: Use a specific release TAG (v2.28.8) instead of a branch ---
RUN git clone -b v2.28.8 --depth 1 https://github.com/Mbed-TLS/mbedtls.git

COPY stats_logger.cpp .

# Compile statically
RUN g++ -static -O3 \
    -I ./mbedtls/include \
    stats_logger.cpp \
    ./mbedtls/library/*.c \
    -o stats_logger \
    -lpthread