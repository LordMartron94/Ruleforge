package parsing

import (
	"fmt"
	shared2 "github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/parsing/rules/shared"

	"github.com/LordMartron94/Ruleforge/ruleforge/components/ruleforge/common/compiler/lexing/shared"
)

type Ruleset[T shared.TokenTypeConstraint] struct {
	Rules []shared2.ParsingRuleInterface[T]
}

func NewRuleset[T shared.TokenTypeConstraint](rules []shared2.ParsingRuleInterface[T]) *Ruleset[T] {
	return &Ruleset[T]{Rules: rules}
}

func (rs *Ruleset[T]) GetMatchingRule(input []*shared.Token[T], currentIndex int) (shared2.ParsingRuleInterface[T], error) {
	for _, rule := range rs.Rules {
		_, err, _ := rule.Match(input, currentIndex)

		if err == nil {
			//fmt.Println(fmt.Sprintf("Matched rule (ruleSet Matcher): %s for input '%s'", rule.Symbol(), input[currentIndex].Value))
			return rule, nil
		}
	}

	return nil, fmt.Errorf("no matching rule found for input '%s'", input[currentIndex].String())
}
