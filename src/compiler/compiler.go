package compiler

import (
	"autarch/pattern"
	"fmt"
	"foundation/domain"
	"foundation/system"
	"langspec"
	"langspec/bootstrap"
	"langspec/dsl"
	"langspec/editor"
	"langspec/toolchain"
	"memarch"
)

// ----------------------------------------------------------------- TYPE ALIASES

type LangParser = langspec.LangParser[rune, string, string, string, string]

type EditorCtx = editor.EditorCtx[rune, string, string, string, string]
type EditorOverride = editor.EditorOverride[rune, string, string, string, string, toolchain.SublimeContext]

// ----------------------------------------------------------------- VARS

var runeFactory = pattern.RegulaASTFactoryCreate(domain.DiscreteDomainRuneCreate())

// ----------------------------------------------------------------- CONFIG

type RuleforgeCompilerConfiguration struct {
	ruleForgeSpecFile string

	scratchAllocationFn memarch.AllocationFn

	diagnosticSink *dsl.LangSpecDiagnosticSink
}

func RuleforgeCompilerConfigurationCreate(
	ruleForgeSpecFile string,
	scratchAllocationFn memarch.AllocationFn,
) *RuleforgeCompilerConfiguration {
	return &RuleforgeCompilerConfiguration{
		ruleForgeSpecFile:   ruleForgeSpecFile,
		scratchAllocationFn: scratchAllocationFn,
		diagnosticSink:      dsl.DefaultLangSpecDiagnosticSink(),
	}
}

func (r *RuleforgeCompilerConfiguration) WithDiagnosticSink(sink *dsl.LangSpecDiagnosticSink) *RuleforgeCompilerConfiguration {
	r.diagnosticSink = sink
	return r
}

// ----------------------------------------------------------------- COMPILER

type RuleforgeCompiler struct {
	parser *LangParser
}

func RuleforgeCompilerCreate(config *RuleforgeCompilerConfiguration) (*RuleforgeCompiler, error) {
	parser, err := bootstrap.CompileParserFromSpec(
		config.ruleForgeSpecFile,
		config.scratchAllocationFn,
		bootstrap.WithDiagnosticSink(config.diagnosticSink),
		bootstrap.WithSublimeOverrides(ruleforgeOverrideProducer()),
	)

	if err != nil {
		return nil, fmt.Errorf("ruleforge compiler bootstrap failed: %w", err)
	}

	return &RuleforgeCompiler{
		parser: parser,
	}, nil
}

func ruleforgeOverrideProducer() func(ec *EditorCtx) (*EditorOverride, bool) {
	registry := editor.NewOverrideRegistry[rune, string, string, string, string, toolchain.SublimeContext]()
	patterns := editor.NewTextPatternBuilder[string, string, string, string, toolchain.SublimeContext](runeFactory)

	registry.Register("TokLineComment", patterns.LineComment(
		"//",
		toolchain.SublimeContext{Scope: "comment.line.double-slash"},
		toolchain.SublimeContext{Scope: "punctuation.definition.comment"},
	))

	registry.Register("TokBlockComment", patterns.BlockComment(
		"/*",
		"*/",
		toolchain.SublimeContext{MetaScope: "comment.block"},
		toolchain.SublimeContext{Scope: "punctuation.definition.comment.begin"},
		toolchain.SublimeContext{Scope: "punctuation.definition.comment.end"},
	))

	return registry.Producer()
}

func RuleforgeCompilerDestroy(compiler *RuleforgeCompiler) {
	langspec.LangParserDestroy(compiler.parser)
}

func RuleforgeCompilerCompile(
	compiler *RuleforgeCompiler,
	file string,
) error {
	if !system.PathHasExt(file, ".rf") {
		return fmt.Errorf("file '%s' is not a ruleforge (.rf) file", file)
	}

	lexSession := langspec.LangParserSessionCreate[rune](file, nil, false)
	lexemes, err := langspec.LangParserLexFile(compiler.parser, lexSession)
	if err != nil {
		return fmt.Errorf("lexing failed: %w", err)
	}

	for i, lexeme := range lexemes {
		fmt.Printf("%03d) %s\n", i, lexeme.DebugString(
			func(s string) string { return s },
			func(s string) string { return s },
		))
	}

	return nil
}
