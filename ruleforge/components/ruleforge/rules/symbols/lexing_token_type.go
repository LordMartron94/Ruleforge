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
	OpenSquareBracketToken
	CloseSquareBracketToken

	// OPERATORS
	AssignmentOperatorToken
	ChainOperatorToken
	GreaterThanOrEqualOperatorToken
	LessThanOrEqualOperatorToken
	GreaterThanOperatorToken
	LessThanOperatorToken
	ExactMatchOperatorToken
	StyleCombineToken
	RuleStrictnessIndicatorToken
	NotEqualToOperatorToken

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
	BuildKeywordToken

	// CLASSES
	MeleeSpellHybridBuildToken
	MeleeDexHybridBuildToken
	SpellDexHybridBuildToken
	MeleeBuildToken
	SpellBuildToken
	DexBuildToken

	// MISC
	DotToken
	FunctionKeywordToken
)
