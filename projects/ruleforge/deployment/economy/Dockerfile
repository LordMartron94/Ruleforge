# syntax=docker/dockerfile:1
FROM --platform=$BUILDPLATFORM golang:1.25.10-alpine AS build
ARG TARGETOS
ARG TARGETARCH
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN GOMAXPROCS=2 CGO_ENABLED=0 GOOS=$TARGETOS GOARCH=$TARGETARCH go build -p 2 -mod=readonly -trimpath -ldflags='-s -w' -o /out/economy-history ./cmd/economy-history
RUN mkdir -p /out/data && chown 65532:65532 /out/data

FROM scratch
LABEL org.opencontainers.image.source="https://github.com/LordMartron94/Ruleforge"
LABEL org.opencontainers.image.description="Ruleforge PoE economy history collector and read-only synchronization API"
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
COPY --from=build /out/economy-history /economy-history
COPY --from=build --chown=65532:65532 /out/data /data
USER 65532:65532
WORKDIR /data
VOLUME ["/data"]
EXPOSE 8087
HEALTHCHECK --interval=60s --timeout=10s --start-period=15s --retries=3 CMD ["/economy-history", "-healthcheck", "http://127.0.0.1:8087"]
ENTRYPOINT ["/economy-history"]
