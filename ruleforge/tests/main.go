package main

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
	"log"
	"os"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/validation"
)

func main() {
	if err := run(); err != nil {
		log.Println("If you struggle to understand the error, you can contact the developer on Discord (mr.hoornasp.learningexpert) or through e-mail: md.career@protonmail.com")
		log.Fatalf("fatal: %v", err)
	}
}

func run() error {
	// 1) Open the input file
	file, err := openFile("test_input/test_filter.rf")
	if err != nil {
		return err
	}
	defer closeFile(file)

	// 2) Build the compiler/file handler
	handler := newFileHandler(file)

	// 3) Lexing
	lexemes, err := handler.Lex()
	if err != nil {
		return fmt.Errorf("lexing file: %w", err)
	}
	printLexemes(lexemes)
	fmt.Println("----------------")

	// 4) Parsing
	tree, err := handler.Parse()
	if err != nil {
		return fmt.Errorf("parsing file: %w", err)
	}
	fmt.Println("----------------")
	tree.Print(2, []symbols.LexingTokenType{symbols.NewLineToken})

	// 5) Validation
	if err := validation.NewParseTreeValidator(tree).Validate(); err != nil {
		return fmt.Errorf("validating parse tree: %w", err)
	}

	return nil
}

func openFile(path string) (*os.File, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("opening %s: %w", path, err)
	}
	return f, nil
}

func closeFile(f *os.File) {
	if err := f.Close(); err != nil {
		log.Printf("warning: closing file: %v", err)
	}
}

func newFileHandler(f *os.File) compiler.FileHandler[symbols.LexingTokenType] {
	lexingRules := rules.NewRuleFactory().GetLexingRules()
	parsingRules := rules.NewDSLParsingRules().GetParsingRules()
	return *compiler.NewFileHandler(f, lexingRules, parsingRules, symbols.IgnoreToken)
}

func printLexemes(lexemes []*shared.Token[symbols.LexingTokenType]) {
	for i, lex := range lexemes {
		fmt.Printf("Lexeme (%d): %q\n", i, lex.String())
	}
}
