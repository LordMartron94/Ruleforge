package data_generation

import lua "github.com/yuin/gopher-lua"

func tableToBoolMap(table *lua.LTable, key string) map[string]bool {
	result := make(map[string]bool)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(k, v lua.LValue) {
			if boolVal, ok := v.(lua.LBool); ok {
				result[k.String()] = bool(boolVal)
			}
		})
	}
	return result
}

func tableToStringMap(table *lua.LTable, key string) map[string]string {
	result := make(map[string]string)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(k, v lua.LValue) {
			result[k.String()] = v.String()
		})
	}
	return result
}

func tableToNestedStringSlice(table *lua.LTable, key string) [][]string {
	var result [][]string
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(_, value lua.LValue) {
			if innerTable, ok := value.(*lua.LTable); ok {
				var modGroup []string
				innerTable.ForEach(func(_, innerValue lua.LValue) {
					modGroup = append(modGroup, innerValue.String())
				})
				result = append(result, modGroup)
			}
		})
	}
	return result
}

func tableToInterfaceMap(table *lua.LTable, key string) map[string]int {
	result := make(map[string]int)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(k, v lua.LValue) {
			if num, ok := v.(lua.LNumber); ok {
				result[k.String()] = int(num)
			}
		})
	}
	return result
}

func getBoolField(table *lua.LTable, key string, defaultValue bool) bool {
	val := table.RawGetString(key)
	if b, ok := val.(lua.LBool); ok {
		return bool(b)
	}
	return defaultValue
}

func getStringField(table *lua.LTable, key string, defaultValue string) string {
	val := table.RawGetString(key)
	if s, ok := val.(lua.LString); ok {
		return string(s)
	}
	return defaultValue
}

func getIntField(table *lua.LTable, key string, defaultValue int) int {
	val := table.RawGetString(key)
	if n, ok := val.(lua.LNumber); ok {
		return int(n)
	}
	return defaultValue
}

func getNumberFieldFloat(table *lua.LTable, key string, defaultValue float64) float64 {
	val := table.RawGetString(key)
	if n, ok := val.(lua.LNumber); ok {
		return float64(n)
	}
	return defaultValue
}

func getListStringField(table *lua.LTable, key string) []string {
	result := make([]string, 0)
	if subTable, ok := table.RawGetString(key).(*lua.LTable); ok {
		subTable.ForEach(func(_, value lua.LValue) {
			result = append(result, value.String())
		})
	}
	return result
}
