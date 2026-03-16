# ───────────────────────────────────────────────────────────────────────
# CONFIGURATION
# ───────────────────────────────────────────────────────────────────────
BINARY_NAME  := build/ruleforge
TEST_BINARY  := build/ruleforge_tests.test
CACHE_DIR    := .build_cache
ROOT_ANCHORS := libs src client

# Find all go.mod files to map the module structure
MODULE_DIRS := $(shell find $(ROOT_ANCHORS) -maxdepth 3 -name "go.mod" -exec dirname {} \;)

# Map module directories to timestamp files in the central cache
# e.g., libs/auth -> .build_cache/libs_auth.gen
GEN_TIMESTAMPS := $(foreach dir,$(MODULE_DIRS),$(CACHE_DIR)/$(subst /,_,$(dir)).gen)

# ───────────────────────────────────────────────────────────────────────
# CORE TARGETS
# ───────────────────────────────────────────────────────────────────────

.PHONY: all generate build test clean dev help

all: generate build ## Run generation then build the binary

$(CACHE_DIR):
	@mkdir -p $(CACHE_DIR)

## 🧠 Centralized Code Generation
# This triggers ONLY if source files in a module are newer than the cache file.
generate: $(CACHE_DIR) $(GEN_TIMESTAMPS)

# Define the pattern for generation logic
# $(1): The module directory
# $(2): The timestamp file in the central cache
define GEN_RULE
$(2): $(shell find $(1) -type f \( -name "*.go" -o -name "*.sql" -o -name "*.proto" \) -not -name "*_gen.go" 2>/dev/null)
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "➜ Generating: $(1)"
	@cd $(1) && go generate ./...
	@touch $(2)
	@echo "✅ OK: $(1)"
endef

# Dynamically generate the rules for every discovered module
$(foreach dir,$(MODULE_DIRS),$(eval $(call GEN_RULE,$(dir),$(CACHE_DIR)/$(subst /,_,$(dir)).gen)))

## 🛠 Build Mode
build: generate $(SASS_OUT)
	@echo "● Compiling Go binary..."
	@mkdir -p $(dir $(BINARY_NAME))
	@go build -v -o $(BINARY_NAME) ./client
	@echo "✅ Build completed."

## 🧪 Testing Mode
test: generate
	@echo "🧪 Building and running tests..."
	@mkdir -p build
	@go test -tags=memforge_debug -c -o $(TEST_BINARY) ./tests
	@./$(TEST_BINARY) -test.v $(ARGS)

clean:
	@rm -rf build/
	@rm -rf $(CACHE_DIR)
	@echo "🧹 Cleaned build artifacts and central cache."

help:
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-15s\033[0m %s\n", $$1, $$2}'