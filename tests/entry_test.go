package tests

import (
	foundationtesting "foundation/testing"
	"memforge"
	"testing"
)

func TestEntryPoint(t *testing.T) {
	defer memforge.MemforgeMemoryDebug()

	testWithTempOkMessages(testCompiler, t)
}

func testWithTempOkMessages(test func(t *testing.T), t *testing.T) {
	foundationtesting.EnableOkMessagesSet(true)
	test(t)
	foundationtesting.EnableOkMessagesSet(false)
}
