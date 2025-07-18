package lexing

import (
	"fmt"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/rules"
	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/scanning"
)

type Ruleset[T shared.TokenTypeConstraint] struct {
	Rules []rules.LexingRuleInterface[T]
}

func NewRuleset[T shared.TokenTypeConstraint](rules []rules.LexingRuleInterface[T]) *Ruleset[T] {
	return &Ruleset[T]{Rules: rules}
}

// GetMatchingRule returns the first matching rule for the given input stream.
// If no matching rule is found, it returns an error.
// If the input stream is exhausted before a matching rule is found, it returns io.EOF.
// Returns the number of runes that will be consumed by the matching rule.
func (rs *Ruleset[T]) GetMatchingRule(scanner scanning.PeekInterface) (rules.LexingRuleInterface[T], error) {
	_ = scanner.Current()

	for _, rule := range rs.Rules {
		matched := rule.IsMatch(scanner)

		if matched {
			//fmt.Println(fmt.Sprintf("Matched rule (ruleSet Matcher): %s for first character '%s'", rule.Symbol(), string(input)))
			return rule, nil
		}
	}

	return nil, fmt.Errorf("no matching rule found\n")
}
