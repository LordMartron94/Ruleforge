package compiler

import (
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing"
	shared3 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"
	parseShared "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/shared"
	"io"
)

// FileHandler is a utility struct for Ruleforge file handling.
type FileHandler[T shared.TokenTypeConstraint] struct {
	lexer  *lexing.Lexer[T]
	parser *parsing.Parser[T]
}

func NewFileHandler[T shared.TokenTypeConstraint](reader io.Reader, lexingRules []rules.LexingRuleInterface[T], parsingRules []shared3.ParsingRuleInterface[T], ignoreTokenType T) *FileHandler[T] {
	lexer := lexing.NewLexer[T](reader, lexingRules)

	return &FileHandler[T]{
		lexer:  lexer,
		parser: parsing.NewParser[T](lexer, parsingRules, ignoreTokenType),
	}
}

func (fh *FileHandler[T]) Lex() ([]*shared.Token[T], error) {
	return fh.lexer.GetTokens()
}

func (fh *FileHandler[T]) Parse() (*parseShared.ParseTree[T], error) {
	return fh.parser.Parse()
}

func (fh *FileHandler[T]) ResetLexer() {
	fh.lexer.Reset()
}
