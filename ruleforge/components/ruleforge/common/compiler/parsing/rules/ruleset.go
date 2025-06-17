package rules

import (
	"fmt"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

type Ruleset[T shared.TokenTypeConstraint] struct {
	Rules []ParsingRuleInterface[T]
}

func NewRuleset[T shared.TokenTypeConstraint](rules []ParsingRuleInterface[T]) *Ruleset[T] {
	return &Ruleset[T]{Rules: rules}
}

func (rs *Ruleset[T]) GetMatchingRule(input []*shared.Token[T], currentIndex int) (ParsingRuleInterface[T], error) {
	for _, rule := range rs.Rules {
		_, err, _ := rule.Match(input, currentIndex)

		if err == nil {
			//fmt.Println(fmt.Sprintf("Matched rule (ruleSet Matcher): %s for input '%s'", rule.Symbol(), input[currentIndex].Value))
			return rule, nil
		}
	}

	return nil, fmt.Errorf("no matching rule found for input '%s'", input[currentIndex].String())
}
