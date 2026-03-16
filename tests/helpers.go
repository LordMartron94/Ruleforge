package tests

import (
	"memarch"
	"memcore"
	"memforge"
)

func setupScratchAllocator() (
	scratchAllocationFn memarch.AllocationFn,
	teardownFn func(),
) {
	scratchAllocator := memforge.DynamicLinearAllocatorCreateFunction(uint64(memcore.KiloByte), func(currentCap, neededCap uint64) uint64 {
		newSize := max(currentCap*2, neededCap)
		if newSize > uint64(memcore.GigaByte) {
			panic("too much memory for a test")
		}
		return newSize
	})

	scratchAllocFn := func(sizeBytes, alignment uint64) memcore.MarkRaw {
		return memforge.DynamicLinearAllocatorMallocUnsafe(scratchAllocator, sizeBytes, alignment)
	}

	return scratchAllocFn, func() { memforge.DynamicLinearAllocatorDestroy(scratchAllocator) }
}
