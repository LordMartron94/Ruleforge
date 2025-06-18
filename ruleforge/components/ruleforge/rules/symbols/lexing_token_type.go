package symbols

//go:generate go run golang.org/x/tools/cmd/stringer@latest -type=LexingTokenType
type LexingTokenType int

//goland:noinspection GoCommentStart
const (
	// CORE TOKENS
	IgnoreToken LexingTokenType = iota
	NewLineToken
	WhitespaceToken
	NumberToken
	LetterToken

	IdentifierKeyToken
	IdentifierValueToken

	VariableReferenceToken

	// BLOCKS
	OpenCurlyBracketToken
	CloseCurlyBracketToken

	// OPERATORS
	AssignmentOperatorToken
	ChainOperatorToken
	GreaterThanOrEqualOperatorToken
	LessThanOrEqualOperatorToken
	GreaterThanOperatorToken
	LessThanOperatorToken
	ExactMatchOperatorToken

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
	VariableKeywordToken
	SectionConditionsKeywordToken
	ConditionAssignmentKeywordToken
	ConditionKeywordToken
	SectionKeywordToken
	DescriptionAssignmentKeywordToken
	RuleKeywordToken

	// MISC
	DotToken
)
