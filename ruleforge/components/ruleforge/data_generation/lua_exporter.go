package data_generation

import (
	"fmt"
	lua "github.com/yuin/gopher-lua"
)

// LuaExecutor is a reusable service for executing Lua scripts.
type LuaExecutor struct{}

// NewLuaExecutor creates a new instance of the Lua executor.
func NewLuaExecutor() *LuaExecutor {
	return &LuaExecutor{}
}

// ExecuteScriptAsFunc loads a Lua script designed to be a function
// and calls it by passing a single Lua table as an argument. It returns the
// populated table. This pattern is common in the Path of Building source.
func (e *LuaExecutor) ExecuteScriptAsFunc(luaFilePath string) (*lua.LTable, error) {
	// 1. Initialize a fresh, isolated Lua state.
	L := lua.NewState()
	defer L.Close()

	// 2. Load the file, which compiles it into a function but does not run it.
	fn, err := L.LoadFile(luaFilePath)
	if err != nil {
		return nil, fmt.Errorf("could not load lua file '%s': %w", luaFilePath, err)
	}

	// 3. Create the table that we will pass as an argument.
	// The Lua script is expected to populate this table.
	argTable := L.NewTable()

	// 4. Push the function and its argument onto the Lua stack.
	L.Push(fn)
	L.Push(argTable)

	// 5. Call the function with 1 argument, expecting 0 return values.
	if err := L.PCall(1, 0, nil); err != nil {
		return nil, fmt.Errorf("error executing lua script '%s': %w", luaFilePath, err)
	}

	// 6. Return the now-populated table.
	return argTable, nil
}

// ExecuteScriptWithReturn handles scripts that return a table directly.
func (e *LuaExecutor) ExecuteScriptWithReturn(luaFilePath string) (*lua.LTable, error) {
	L := lua.NewState()
	defer L.Close()

	// Loading is the same, it compiles the file into a function.
	fn, err := L.LoadFile(luaFilePath)
	if err != nil {
		return nil, fmt.Errorf("could not load lua file '%s': %w", luaFilePath, err)
	}

	L.Push(fn)
	// Execute with 0 arguments, but expect 1 return value.
	if err := L.PCall(0, 1, nil); err != nil {
		return nil, fmt.Errorf("error executing lua script '%s': %w", luaFilePath, err)
	}

	// Get the return value from the top of the stack.
	ret := L.Get(-1)

	// Check if the return value is actually a table.
	if tbl, ok := ret.(*lua.LTable); ok {
		return tbl, nil
	}

	return nil, fmt.Errorf("lua script did not return a table")
}
