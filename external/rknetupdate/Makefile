GO ?= go

# Rootfs builds can run under sudo with HOME and Go cache variables removed.
ifeq ($(strip $(HOME)),)
HOME := $(CURDIR)/.build-home
endif
ifeq ($(strip $(XDG_CACHE_HOME)),)
XDG_CACHE_HOME := $(CURDIR)/.build-cache
endif
ifeq ($(strip $(GOCACHE)),)
GOCACHE := $(XDG_CACHE_HOME)/go-build
endif
export HOME XDG_CACHE_HOME GOCACHE

BINARY := rknetupdate
CONTROL_BINARY := rknetupdate-control
RECOVERY_BINARY := rknetupdate-recovery
OUT := output/bin
GOFLAGS := -trimpath
LDFLAGS := -s -w

.PHONY: all fmt test test-recovery-loop test-recovery-notice-contract test-recovery-notice-integration test-recovery-mmc-detection test-recovery-sd-config test-recovery-sd-handoff test-recovery-flow-contract test-rknetupdate-handoff-validation test-bundle-migration-loop test-updateimg-migration-loop test-sdk-recovery-handoff check-sdk-recovery-integration check-sdk-filesystem-integration vet build arm64 control control-arm64 recovery recovery-arm64 all-arm64 clean distclean

all: test vet build control recovery arm64 control-arm64 recovery-arm64

fmt:
	$(GO) fmt ./...

test:
	CGO_ENABLED=0 $(GO) test ./...

vet:
	CGO_ENABLED=0 $(GO) vet ./...

build:
	mkdir -p $(OUT)
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 $(GO) build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(OUT)/$(BINARY)-linux-amd64 ./cmd/rknetupdate

recovery:
	mkdir -p $(OUT)
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 $(GO) build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(OUT)/$(RECOVERY_BINARY)-linux-amd64 ./cmd/rknetupdate-recovery

control:
	mkdir -p $(OUT)
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 $(GO) build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(OUT)/$(CONTROL_BINARY)-linux-amd64 ./cmd/rknetupdate-control

arm64:
	mkdir -p $(OUT)
	CGO_ENABLED=0 GOOS=linux GOARCH=arm64 $(GO) build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(OUT)/$(BINARY)-linux-arm64 ./cmd/rknetupdate

recovery-arm64:
	mkdir -p $(OUT)
	CGO_ENABLED=0 GOOS=linux GOARCH=arm64 $(GO) build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(OUT)/$(RECOVERY_BINARY)-linux-arm64 ./cmd/rknetupdate-recovery

all-arm64: arm64 control-arm64 recovery-arm64

control-arm64:
	mkdir -p $(OUT)
	CGO_ENABLED=0 GOOS=linux GOARCH=arm64 $(GO) build $(GOFLAGS) -ldflags "$(LDFLAGS)" -o $(OUT)/$(CONTROL_BINARY)-linux-arm64 ./cmd/rknetupdate-control

test-recovery-loop: recovery
	./scripts/test-recovery-loop.sh $(OUT)/$(RECOVERY_BINARY)-linux-amd64

test-recovery-notice-contract:
	./scripts/test-recovery-notice-contract.sh

test-recovery-notice-integration:
	./scripts/test-recovery-notice-integration.sh

test-recovery-mmc-detection:
	./scripts/test-recovery-mmc-detection.sh

test-recovery-sd-config:
	./scripts/test-recovery-sd-config.sh

test-recovery-sd-handoff:
	./scripts/test-recovery-sd-handoff.sh

test-recovery-flow-contract:
	./scripts/test-recovery-flow-contract.sh

test-rknetupdate-handoff-validation:
	./scripts/test-rknetupdate-handoff-validation.sh

test-bundle-migration-loop:
	./scripts/test-bundle-migration-loop.sh

test-updateimg-migration-loop:
	./scripts/test-updateimg-migration-loop.sh

test-sdk-recovery-handoff:
	./scripts/test-sdk-recovery-handoff.sh

check-sdk-recovery-integration:
	./scripts/integrate-recovery-sdks.sh --check

check-sdk-filesystem-integration:
	./scripts/integrate-filesystem-sdks.sh --check

clean:
	rm -rf output .playwright-cli

distclean: clean
	rm -rf .build-home .build-cache .go-cache-user
