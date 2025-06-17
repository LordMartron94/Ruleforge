package extensions

import (
	"fmt"
	"slices"
	"sort"
)

// FindNumberOfMatchesInSlice returns the amount of occurrences for the target value in the given slice.
// If reverseAllowed is true, the search will be performed in reverse order as well.
func FindNumberOfMatchesInSlice[T comparable](slice []T, targets []T, reverseAllowed bool) int {
	matches := 0

	for i := 0; i < len(slice)-len(targets)+1; i++ {
		match := true
		for j := 0; j < len(targets); j++ {
			if slice[i+j] != targets[j] {
				match = false
				break
			}
		}
		if match {
			matches++
		}
	}

	if reverseAllowed {
		newTargets := make([]T, len(targets))
		copy(newTargets, targets)
		slices.Reverse(newTargets)

		matches += FindNumberOfMatchesInSlice[T](slice, targets, false)
	}

	return matches
}

func FindNumberOfMatchesInSliceV2[T any](slice []T, targets []T, reverseAllowed bool, equalityCheckFunc func(a, b T) bool) int {
	matches := 0

	for i := 0; i < len(slice)-len(targets)+1; i++ {
		match := true
		for j := 0; j < len(targets); j++ {
			if !equalityCheckFunc(slice[i+j], targets[j]) {
				match = false
				break
			}
		}
		if match {
			matches++
		}
	}

	if reverseAllowed {
		newTargets := make([]T, len(targets))
		copy(newTargets, targets)
		slices.Reverse(newTargets)

		matches += FindNumberOfMatchesInSliceV2[T](slice, targets, false, equalityCheckFunc)
	}

	return matches
}

// GetFormattedString returns a formatted string representation of the given slice.
func GetFormattedString[T any](slice []T) string {
	formattedString := ""
	for _, item := range slice {
		formattedString += fmt.Sprintf("%v, ", item)
	}
	if len(formattedString) > 0 {
		formattedString = "[" + formattedString[:len(formattedString)-2] + "]"
	} else {
		formattedString = "[" + formattedString + "]"
	}

	return formattedString
}

// GetFormattedStringSorted returns a formatted string representation of the given slice but with a sort function.
// Useful if you don't want to sort the original slice, but only the printed output.
func GetFormattedStringSorted[T any](slice []T, sortComparison func(a, b T) bool) string {
	sortedSlice := make([]T, len(slice))
	copy(sortedSlice, slice)

	sort.Slice(sortedSlice, func(i, j int) bool {
		return sortComparison(sortedSlice[i], sortedSlice[j])
	})

	formattedString := ""
	for _, item := range sortedSlice {
		formattedString += fmt.Sprintf("%v, ", item)
	}
	if len(formattedString) > 0 {
		formattedString = "[" + formattedString[:len(formattedString)-2] + "]"
	} else {
		formattedString = "[" + formattedString + "]"
	}

	return formattedString
}

func Contains[T comparable](slice []T, toCompare T) bool {
	for _, x := range slice {
		if x == toCompare {
			return true
		}
	}
	return false
}
