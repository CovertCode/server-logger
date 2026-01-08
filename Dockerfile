FROM alpine:3.18
RUN apk add --no-cache g++ make git
WORKDIR /app
CMD git clone -b v2.28 --depth 1 https://github.com/Mbed-TLS/mbedtls.git && \
    g++ -static -O3 -I ./mbedtls/include \
    main.cpp ./mbedtls/library/*.c \
    -o stats_logger && \
    cp stats_logger /out/