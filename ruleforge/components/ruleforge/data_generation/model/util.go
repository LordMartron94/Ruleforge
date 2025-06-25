package model

import "strings"

func sanitizeBaseType(baseType string) string {
	startIndex := strings.Index(baseType, "(")
	if startIndex == -1 {
		return strings.TrimSpace(baseType)
	}
	endIndex := strings.LastIndex(baseType, ")")
	if endIndex == -1 || endIndex < startIndex {
		return strings.TrimSpace(baseType)
	}
	result := baseType[:startIndex]
	if endIndex < len(baseType)-1 {
		result += baseType[endIndex+1:]
	}
	return strings.TrimSpace(result)
}
