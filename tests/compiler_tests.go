package tests

import (
	"ruleforge/compiler"
	"testing"
)

const ruleForgeSpecFile = "src/compiler/ruleforge.lspec"
const ruleForgeTestFile = "assets/testing/input.rf"

func testCompiler(t *testing.T) {
	scratchAllocFn, teardown := setupScratchAllocator()
	defer teardown()

	compilerConfig := compiler.RuleforgeCompilerConfigurationCreate(
		ruleForgeSpecFile,
		scratchAllocFn,
	)

	rfCompiler, err := compiler.RuleforgeCompilerCreate(compilerConfig)
	if err != nil {
		t.Fatalf("creation of ruleforge compiler failed: %s", err.Error())
	}

	defer compiler.RuleforgeCompilerDestroy(rfCompiler)

	err = compiler.RuleforgeCompilerCompile(rfCompiler, ruleForgeTestFile)
	if err != nil {
		t.Fatalf("parsing of ruleforge test input failed: %s", err.Error())
	}
}
