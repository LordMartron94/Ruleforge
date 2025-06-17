package definitions

//go:generate go run golang.org/x/tools/cmd/stringer@latest -type=LexingTokenType
type LexingTokenType int

//goland:noinspection GoCommentStart
const (
	// CORE TOKENS
	IgnoreToken LexingTokenType = iota
	NewLineToken
	WhitespaceToken
	DigitToken
	LetterToken
	IdentifierToken

	// BLOCKS
	CurlyBracketToken

	// OPERATORS
	AssignmentOperatorToken

	// KEYWORDS
	MetadataKeywordToken
	NameKeywordToken
	VersionKeywordToken
	StrictnessKeywordToken
	AllKeywordToken
	SoftKeywordToken
	SemiStrictKeywordToken
	StrictKeywordToken
	SuperStrictKeywordToken

	// MISC
	DotToken
)
