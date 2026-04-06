# Stage 1: Build
FROM alpine:3.19 AS builder
RUN apk add --no-cache cmake make g++ linux-headers openssl-dev curl-dev sqlite-dev git
WORKDIR /build
COPY . .
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --target clove_kernel clove_cli -j$(nproc)

# Stage 2: Runtime
FROM alpine:3.19
RUN apk add --no-cache libstdc++ openssl libcurl sqlite-libs
COPY --from=builder /build/build/kernel/clove_kernel /usr/local/bin/
COPY --from=builder /build/build/cli/clove_cli /usr/local/bin/clove
EXPOSE 8080 8081
ENTRYPOINT ["clove_kernel"]
CMD ["--socket", "/tmp/clove.sock", "--db", "/data/clove.db", "--api", "--api-port", "8080", "--openrouter", "--mcp", "--a2a"]
