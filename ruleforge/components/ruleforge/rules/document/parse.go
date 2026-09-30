package document

import (
	"fmt"
	"sort"
	"strings"

	compiler "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/rules/symbols"
)

// token is a lexer token with its place in the source.
type token struct {
	typ        symbols.LexingTokenType
	text       string // the token's value; quotes are not part of it
	start, end int    // byte offsets in the source, quotes included
	quoted     bool
}

// Parse reads a script with the compiler's own lexer. It only checks the
// layout it needs to build the document; the compiler validates the script.
func Parse(src string) (*Document, error) {
	tokens, err := tokenize(src)
	if err != nil {
		return nil, err
	}
	p := &parser{src: src, toks: tokens, lines: lineStarts(src)}
	items, _, _, err := p.lines_(contextTop)
	if err != nil {
		return nil, err
	}
	return &Document{Items: items, FinalNewline: src == "" || strings.HasSuffix(src, "\n")}, nil
}

func tokenize(src string) (tokens []token, err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("the script could not be read: %v", r)
		}
	}()
	handler := compiler.NewFileHandler(strings.NewReader(src), rules.GetLexingRules(), rules.GetParsingRules(), symbols.IgnoreToken)
	lexed, err := handler.Lex()
	if err != nil {
		return nil, err
	}
	offset := 0
	for _, t := range lexed {
		value := string(t.Value)
		tok := token{typ: t.Type, text: value, start: offset}
		if t.Type == symbols.IdentifierValueToken && offset < len(src) && src[offset] == '"' {
			end := offset + 1 + len(value)
			if end >= len(src) || src[offset+1:end] != value || src[end] != '"' {
				return nil, fmt.Errorf("could not place the text %q on line %d", value, lineOf(src, offset))
			}
			tok.quoted = true
			tok.end = end + 1
		} else {
			if !strings.HasPrefix(src[offset:], value) {
				return nil, fmt.Errorf("could not place the text %q on line %d", value, lineOf(src, offset))
			}
			tok.end = offset + len(value)
		}
		if tok.end == tok.start {
			return nil, fmt.Errorf("the lexer returned an empty token on line %d", lineOf(src, offset))
		}
		offset = tok.end
		tokens = append(tokens, tok)
	}
	if offset != len(src) {
		return nil, fmt.Errorf("the text from line %d on could not be read", lineOf(src, offset))
	}
	return tokens, nil
}

func lineStarts(src string) []int {
	starts := []int{0}
	for i, c := range src {
		if c == '\n' {
			starts = append(starts, i+1)
		}
	}
	return starts
}

func lineOf(src string, offset int) int {
	return strings.Count(src[:min(offset, len(src))], "\n") + 1
}

type context int

const (
	contextTop context = iota
	contextSection
	contextMetadata
	contextConditions
	contextRules
	contextParams
)

type parser struct {
	src   string
	toks  []token
	pos   int
	lines []int
}

func (p *parser) line(offset int) int {
	return sort.Search(len(p.lines), func(i int) bool { return p.lines[i] > offset })
}

func (p *parser) peek() *token {
	if p.pos < len(p.toks) {
		return &p.toks[p.pos]
	}
	return nil
}

func (p *parser) offset() int {
	if t := p.peek(); t != nil {
		return t.start
	}
	return len(p.src)
}

func isComment(t *token) bool {
	return t.typ == symbols.IgnoreToken && strings.HasPrefix(t.text, "!!")
}

// isSpace reports whether a token is layout: spaces, or a character the
// compiler ignores such as a parenthesis.
func isSpace(t *token) bool {
	return t.typ == symbols.WhitespaceToken || (t.typ == symbols.IgnoreToken && !isComment(t))
}

func (p *parser) errorf(format string, args ...any) error {
	return fmt.Errorf("line %d: %s", p.line(p.offset()), fmt.Sprintf(format, args...))
}

// skipSpaces skips spaces on the current line.
func (p *parser) skipSpaces() {
	for t := p.peek(); t != nil && isSpace(t); t = p.peek() {
		p.pos++
	}
}

// skipLayout skips spaces and line breaks inside a construct. A comment there
// has no place in the document, so it is an error.
func (p *parser) skipLayout() error {
	for t := p.peek(); t != nil; t = p.peek() {
		switch {
		case isSpace(t) || t.typ == symbols.NewLineToken:
			p.pos++
		case isComment(t):
			return p.errorf("the editor cannot keep a comment inside a construct; move it to its own line")
		default:
			return nil
		}
	}
	return nil
}

func (p *parser) expect(typ symbols.LexingTokenType, what string) (*token, error) {
	if err := p.skipLayout(); err != nil {
		return nil, err
	}
	t := p.peek()
	if t == nil || t.typ != typ {
		return nil, p.errorf("expected %s", what)
	}
	p.pos++
	return t, nil
}

// restOfLine reads what follows a construct on its line: spaces and at most
// one comment, then the line break, which it consumes. It returns that text.
func (p *parser) restOfLine() (string, error) {
	start := p.offset()
	p.skipSpaces()
	if t := p.peek(); t != nil && isComment(t) {
		p.pos++
		p.skipSpaces()
	}
	end := p.offset()
	t := p.peek()
	switch {
	case t == nil:
	case t.typ == symbols.NewLineToken:
		p.pos++
	default:
		return "", p.errorf("the editor needs every rule, macro and block on its own line")
	}
	return p.src[start:end], nil
}

// lines_ reads line-level items until the block's closing bracket (or the end
// of the file at the top level). It returns the items, the whitespace before
// the closing bracket and the closing bracket's token index.
func (p *parser) lines_(ctx context) ([]*Item, string, int, error) {
	closing := symbols.CloseCurlyBracketToken
	if ctx == contextParams {
		closing = symbols.CloseSquareBracketToken
	}
	var items []*Item
	for {
		indentStart := p.offset()
		p.skipSpaces()
		t := p.peek()
		if t == nil {
			if ctx != contextTop {
				return nil, "", 0, p.errorf("a block is missing its closing bracket")
			}
			if indentStart < len(p.src) {
				addBlank(&items, p.src[indentStart:], false)
			}
			return items, "", -1, nil
		}
		indent := p.src[indentStart:t.start]
		switch {
		case t.typ == symbols.NewLineToken:
			p.pos++
			addBlank(&items, p.src[indentStart:t.end], true)
		case isComment(t):
			p.pos++
			item := &Item{Kind: KindComment, Text: t.text, Raw: t.text, Indent: indent}
			trailing, err := p.restOfLine()
			if err != nil {
				return nil, "", 0, err
			}
			item.Trailing = trailing
			items = append(items, item)
		case ctx != contextTop && t.typ == closing:
			index := p.pos
			p.pos++
			return items, indent, index, nil
		default:
			start := p.pos
			item, err := p.construct(ctx)
			if err != nil {
				if ctx != contextTop || (t.typ != symbols.SectionKeywordToken && t.typ != symbols.MetadataKeywordToken) {
					return nil, "", 0, err
				}
				// Keep a block the form cannot show as editable source text.
				p.pos = start
				if item, err = p.sourceBlock(); err != nil {
					return nil, "", 0, err
				}
			}
			item.Indent = indent
			trailing, err := p.restOfLine()
			if err != nil {
				return nil, "", 0, err
			}
			item.Trailing = trailing
			items = append(items, item)
		}
	}
}

func addBlank(items *[]*Item, text string, lineBreak bool) {
	count := 0
	if lineBreak {
		count = 1
	}
	if n := len(*items); n > 0 && (*items)[n-1].Kind == KindBlank {
		(*items)[n-1].Raw += text
		(*items)[n-1].Count += count
		return
	}
	*items = append(*items, &Item{Kind: KindBlank, Raw: text, Count: count})
}

func (p *parser) construct(ctx context) (*Item, error) {
	t := p.peek()
	start := t.start
	var item *Item
	var err error
	switch {
	case ctx == contextTop && t.typ == symbols.MetadataKeywordToken:
		item, err = p.block(KindMetadata, contextMetadata)
	case ctx == contextTop && t.typ == symbols.ImportKeywordToken:
		item, err = p.importItem()
	case ctx == contextTop && t.typ == symbols.VariableKeywordToken:
		item, err = p.varItem()
	case ctx == contextTop && t.typ == symbols.SectionKeywordToken:
		item, err = p.block(KindSection, contextSection)
	case ctx == contextSection && t.typ == symbols.MetadataKeywordToken:
		item, err = p.block(KindMetadata, contextMetadata)
	case ctx == contextSection && t.typ == symbols.SectionConditionsKeywordToken:
		item, err = p.block(KindConditions, contextConditions)
	case ctx == contextSection && t.typ == symbols.RuleKeywordToken:
		item, err = p.block(KindRules, contextRules)
	case ctx == contextMetadata && isFieldKey(t.typ):
		item, err = p.field()
	case ctx == contextConditions && t.typ == symbols.ConditionAssignmentKeywordToken:
		item, err = p.whereItem(KindWhere)
	case ctx == contextRules && t.typ == symbols.ConditionAssignmentKeywordToken:
		item, err = p.whereItem(KindRule)
	case ctx == contextRules && t.typ == symbols.FunctionKeywordToken:
		item, err = p.macro()
	case ctx == contextParams && t.typ == symbols.ChainOperatorToken:
		item, err = p.param()
	default:
		return nil, p.errorf("unexpected %q here", t.text)
	}
	if err != nil {
		return nil, err
	}
	item.Raw = p.src[start:p.toks[p.pos-1].end]
	return item, nil
}

func isFieldKey(typ symbols.LexingTokenType) bool {
	switch typ {
	case symbols.NameKeywordToken, symbols.VersionKeywordToken, symbols.StrictnessKeywordToken,
		symbols.DescriptionAssignmentKeywordToken, symbols.BuildKeywordToken:
		return true
	}
	return false
}

// block reads KEYWORD { ... } with the opening bracket on the keyword's line.
func (p *parser) block(kind string, ctx context) (*Item, error) {
	p.pos++ // keyword
	p.skipSpaces()
	if t := p.peek(); t == nil || t.typ != symbols.OpenCurlyBracketToken {
		return nil, p.errorf("expected { on the same line")
	}
	p.pos++
	openTrailing, err := p.restOfLine()
	if err != nil {
		return nil, err
	}
	items, closeIndent, _, err := p.lines_(ctx)
	if err != nil {
		return nil, err
	}
	return &Item{Kind: kind, Items: items, OpenTrailing: openTrailing, CloseIndent: closeIndent}, nil
}

// sourceBlock reads a whole bracketed block as source text.
func (p *parser) sourceBlock() (*Item, error) {
	start := p.peek().start
	depth := 0
	for t := p.peek(); t != nil; t = p.peek() {
		p.pos++
		switch t.typ {
		case symbols.OpenCurlyBracketToken:
			depth++
		case symbols.CloseCurlyBracketToken:
			depth--
			if depth == 0 {
				return &Item{Kind: KindSource, Raw: p.src[start:t.end]}, nil
			}
		}
	}
	return nil, fmt.Errorf("line %d: a block is missing its closing bracket", p.line(start))
}

func (p *parser) value(what string) (*Value, error) {
	if err := p.skipLayout(); err != nil {
		return nil, err
	}
	t := p.peek()
	if t == nil {
		return nil, p.errorf("expected %s", what)
	}
	kind := ""
	switch t.typ {
	case symbols.IdentifierValueToken:
		kind = ValueKeyword
		if t.quoted {
			kind = ValueString
		}
	case symbols.VariableReferenceToken:
		kind = ValueVar
	case symbols.NumberToken:
		kind = ValueNumber
	case symbols.IdentifierKeyToken, symbols.AllKeywordToken, symbols.SoftKeywordToken, symbols.SemiStrictKeywordToken,
		symbols.StrictKeywordToken, symbols.SuperStrictKeywordToken, symbols.MeleeBuildToken, symbols.DexBuildToken,
		symbols.SpellBuildToken, symbols.MeleeSpellHybridBuildToken, symbols.MeleeDexHybridBuildToken, symbols.SpellDexHybridBuildToken:
		kind = ValueKeyword
	default:
		return nil, p.errorf("expected %s", what)
	}
	p.pos++
	return &Value{Text: t.text, Kind: kind}, nil
}

func (p *parser) field() (*Item, error) {
	key := p.peek().text
	p.pos++
	if _, err := p.expect(symbols.AssignmentOperatorToken, "=>"); err != nil {
		return nil, err
	}
	value, err := p.value("a value")
	if err != nil {
		return nil, err
	}
	return &Item{Kind: KindField, Key: key, Value: value}, nil
}

func (p *parser) importItem() (*Item, error) {
	p.pos++
	value, err := p.value("a file name")
	if err != nil {
		return nil, err
	}
	return &Item{Kind: KindImport, Value: value}, nil
}

// lookahead runs try and rewinds unless it reports a match.
func (p *parser) lookahead(try func() bool) bool {
	saved := p.pos
	if try() {
		return true
	}
	p.pos = saved
	return false
}

func (p *parser) nextIs(types ...symbols.LexingTokenType) bool {
	return p.lookahead(func() bool {
		for _, typ := range types {
			if p.skipLayout() != nil {
				return false
			}
			t := p.peek()
			if t == nil || t.typ != typ {
				return false
			}
			p.pos++
		}
		return true
	})
}

func (p *parser) condition() (Condition, error) {
	id, err := p.expect(symbols.ConditionKeywordToken, "a condition such as @rarity")
	if err != nil {
		return Condition{}, err
	}
	if err := p.skipLayout(); err != nil {
		return Condition{}, err
	}
	op := p.peek()
	if op == nil || !isComparison(op.typ) {
		return Condition{}, p.errorf("expected a comparison after %s", id.text)
	}
	p.pos++
	value, err := p.value("a value to compare with")
	if err != nil {
		return Condition{}, err
	}
	return Condition{Identifier: id.text, Operator: op.text, Value: *value}, nil
}

func isComparison(typ symbols.LexingTokenType) bool {
	switch typ {
	case symbols.GreaterThanOrEqualOperatorToken, symbols.LessThanOrEqualOperatorToken, symbols.GreaterThanOperatorToken,
		symbols.LessThanOperatorToken, symbols.ExactMatchOperatorToken, symbols.NotEqualToOperatorToken:
		return true
	}
	return false
}

// whereItem reads WHERE a -> b, and for a rule also => style => action [# STRICTNESS].
func (p *parser) whereItem(kind string) (*Item, error) {
	p.pos++ // WHERE
	item := &Item{Kind: kind}
	for {
		condition, err := p.condition()
		if err != nil {
			return nil, err
		}
		item.Conditions = append(item.Conditions, condition)
		if !p.lookahead(func() bool {
			if !p.nextIs(symbols.ChainOperatorToken) {
				return false
			}
			if p.skipLayout() != nil {
				return false
			}
			t := p.peek()
			return t != nil && t.typ == symbols.ConditionKeywordToken
		}) {
			break
		}
	}
	if kind == KindWhere {
		return item, nil
	}
	var err error
	if _, err = p.expect(symbols.AssignmentOperatorToken, "=> and a style"); err != nil {
		return nil, err
	}
	if item.Style, err = p.value("a style"); err != nil {
		return nil, err
	}
	if _, err = p.expect(symbols.AssignmentOperatorToken, "=> and $Show or $Hide"); err != nil {
		return nil, err
	}
	if item.Action, err = p.value("$Show or $Hide"); err != nil {
		return nil, err
	}
	if p.nextIs(symbols.RuleStrictnessIndicatorToken) {
		strictness, err := p.value("a strictness")
		if err != nil {
			return nil, err
		}
		item.Strictness = strictness.Text
	}
	return item, nil
}

func (p *parser) macro() (*Item, error) {
	p.pos++ // MACRO
	if _, err := p.expect(symbols.OpenSquareBracketToken, "["); err != nil {
		return nil, err
	}
	name, err := p.expect(symbols.IdentifierValueToken, "the macro's name")
	if err != nil {
		return nil, err
	}
	item := &Item{Kind: KindMacro, Name: name.text}
	p.skipSpaces()
	t := p.peek()
	if t != nil && (t.typ == symbols.NewLineToken || isComment(t)) {
		// One parameter per line.
		openTrailing, err := p.restOfLine()
		if err != nil {
			return nil, err
		}
		items, closeIndent, _, err := p.lines_(contextParams)
		if err != nil {
			return nil, err
		}
		item.OpenTrailing, item.Items, item.CloseIndent = openTrailing, items, closeIndent
		return item, nil
	}
	item.Inline = true
	for {
		if p.nextIs(symbols.CloseSquareBracketToken) {
			return item, nil
		}
		if err := p.skipLayout(); err != nil {
			return nil, err
		}
		if t := p.peek(); t == nil || t.typ != symbols.ChainOperatorToken {
			return nil, p.errorf("expected -> $parameter or ]")
		}
		param, err := p.param()
		if err != nil {
			return nil, err
		}
		item.Items = append(item.Items, param)
	}
}

func (p *parser) param() (*Item, error) {
	if _, err := p.expect(symbols.ChainOperatorToken, "->"); err != nil {
		return nil, err
	}
	key, err := p.expect(symbols.VariableReferenceToken, "a $parameter")
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(symbols.AssignmentOperatorToken, "=>"); err != nil {
		return nil, err
	}
	value, err := p.value("the parameter's value")
	if err != nil {
		return nil, err
	}
	return &Item{Kind: KindParam, Key: key.text, Value: value}, nil
}

func (p *parser) varItem() (*Item, error) {
	p.pos++ // var
	item := &Item{Kind: KindVar}
	for {
		assignment, err := p.assignment()
		if err != nil {
			return nil, err
		}
		item.Assignments = append(item.Assignments, assignment)
		if !p.nextIs(symbols.ChainOperatorToken, symbols.IdentifierKeyToken, symbols.AssignmentOperatorToken) {
			return item, nil
		}
		// Rewind to the name; assignment reads it again.
		for p.toks[p.pos-1].typ != symbols.IdentifierKeyToken {
			p.pos--
		}
		p.pos--
	}
}

func (p *parser) assignment() (Assignment, error) {
	name, err := p.expect(symbols.IdentifierKeyToken, "a variable name")
	if err != nil {
		return Assignment{}, err
	}
	if _, err := p.expect(symbols.AssignmentOperatorToken, "=>"); err != nil {
		return Assignment{}, err
	}
	assignment := Assignment{Name: name.text}
	for {
		value, err := p.value("a style")
		if err != nil {
			return Assignment{}, err
		}
		assignment.Values = append(assignment.Values, *value)
		if !p.nextIs(symbols.StyleCombineToken) {
			break
		}
	}
	for p.nextIs(symbols.StyleOverrideToken) {
		if _, err := p.expect(symbols.OpenSquareBracketToken, "["); err != nil {
			return Assignment{}, err
		}
		var block []Override
		for {
			source, err := p.value("a style")
			if err != nil {
				return Assignment{}, err
			}
			if _, err := p.expect(symbols.AssignmentOperatorToken, "=>"); err != nil {
				return Assignment{}, err
			}
			property, err := p.expect(symbols.IdentifierValueToken, "a property name")
			if err != nil {
				return Assignment{}, err
			}
			block = append(block, Override{Source: *source, Property: property.text})
			if !p.nextIs(symbols.ChainOperatorToken) {
				break
			}
		}
		if _, err := p.expect(symbols.CloseSquareBracketToken, "]"); err != nil {
			return Assignment{}, err
		}
		assignment.Overrides = append(assignment.Overrides, block)
	}
	return assignment, nil
}
