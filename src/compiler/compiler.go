package compiler

import (
	"autarch/pattern"
	"fmt"
	"foundation/domain"
	"foundation/system"
	"langspec"
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
	compilerConfig := dsl.LangSpecCompilerConfigurationCreate(
		config.scratchAllocationFn,
		nil,
	).WithDiagnosticSink(config.diagnosticSink)

	langSpecCompiler := dsl.LangSpecCompilerCreate(compilerConfig)
	defer dsl.LangSpecCompilerDestroy(langSpecCompiler) // We do not need the langspec compiler after getting the initial compiled result.

	ruleforgeSpecCompileResult, err := dsl.LangSpecCompilerCompile(langSpecCompiler, config.ruleForgeSpecFile)
	if err != nil {
		return nil, fmt.Errorf("compilation failed with error: %w", err)
	}

	err = toolchain.RunSublimeToolchain(ruleforgeSpecCompileResult, ruleforgeOverrideProducer())
	if err != nil {
		return nil, fmt.Errorf("sublime toolchain failed with error: %w", err)
	}

	langSpec := langspec.LangSpecCreate(
		ruleforgeSpecCompileResult.CompiledLexerSpec, ruleforgeSpecCompileResult.CompiledParserSpec,
	)

	parserConfig := langspec.LangParserConfigurationCreate(
		langSpec,
		config.scratchAllocationFn,
	)
	parser := langspec.LangParserCreate(parserConfig)

	return &RuleforgeCompiler{
		parser: parser,
	}, nil
}

func ruleforgeOverrideProducer() func(ec *EditorCtx) (*EditorOverride, bool) {
	return func(ec *EditorCtx) (*EditorOverride, bool) {
		if ec.Token == nil {
			return nil, false
		}

		switch *ec.Token {
		case "TokBlockComment":
			return blockCommentOverride(), true
		case "TokLineCOmment":
			return lineCommentOverride(), true
		default:
			return nil, false
		}
	}
}

func lineCommentOverride() *EditorOverride {
	slashes := pattern.LiteralString(runeFactory, "//").Capture()
	notTerminator := runeFactory.NegatedClass(
		runeFactory.Range('\n', '\n'),
		runeFactory.Range('\r', '\r'),
	).Star().Capture()

	newPattern := slashes.Then(notTerminator)
	matchCtx := toolchain.SublimeContext{Scope: "comment.line.double-slash"}

	return &EditorOverride{
		Pattern:      &newPattern,
		MatchContext: &matchCtx,
		Captures: map[int]toolchain.SublimeContext{
			1: {Scope: "punctuation.definition.comment"},
		},
	}
}

func blockCommentOverride() *EditorOverride {
	openPattern := pattern.LiteralString(runeFactory, "/*")
	closePattern := pattern.LiteralString(runeFactory, "*/")

	return &EditorOverride{
		Pattern:      &openPattern,
		MatchContext: &toolchain.SublimeContext{Scope: "punctuation.definition.comment.begin"},
		DelimitedPayload: &editor.DelimitedPayload[rune, toolchain.SublimeContext]{
			StateLabel:   "block_comment_inner",
			BodyContext:  toolchain.SublimeContext{MetaScope: "comment.block"},
			ClosePattern: closePattern,
			CloseContext: toolchain.SublimeContext{Scope: "punctuation.definition.comment.end"},
		},
	}
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
