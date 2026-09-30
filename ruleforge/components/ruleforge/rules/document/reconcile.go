package document

import "strings"

// Reconcile drops the Raw source text of every item whose text no longer
// means what the item says, so an editor that changed an item but kept its
// Raw cannot lose the change. It returns how many items it reset.
func Reconcile(doc *Document) int {
	return reconcileItems(doc.Items, contextTop)
}

func reconcileItems(items []*Item, ctx context) int {
	reset := 0
	for _, item := range items {
		inner := childContext(item.Kind)
		reset += reconcileItems(item.Items, inner)
		if item.Raw == "" || item.Kind == KindSource {
			continue
		}
		if !rawMatches(item, ctx) {
			item.Raw, item.Indent, item.CloseIndent = "", "", ""
			reset++
		}
	}
	return reset
}

func childContext(kind string) context {
	switch kind {
	case KindSection:
		return contextSection
	case KindMetadata:
		return contextMetadata
	case KindConditions:
		return contextConditions
	case KindRules:
		return contextRules
	case KindMacro:
		return contextParams
	}
	return contextTop
}

func rawMatches(item *Item, ctx context) bool {
	switch item.Kind {
	case KindBlank:
		return strings.Trim(item.Raw, " \t\r\n") == "" && strings.Count(item.Raw, "\n") == item.Count
	case KindComment:
		return item.Raw == item.Text
	}
	parsed, err := parseFragment(item.Raw, ctx)
	if err != nil {
		return false
	}
	return equivalentItems([]*Item{item}, []*Item{parsed}, "") == nil && rawChildrenMatch(item.Items, parsed.Items)
}

// rawChildrenMatch checks that a container's Raw still holds its children:
// the same items and empty lines, in the same order, each with the same text.
func rawChildrenMatch(items, parsed []*Item) bool {
	if len(items) != len(parsed) {
		return false
	}
	for i := range items {
		a, b := items[i], parsed[i]
		if a.Raw != b.Raw || a.Trailing != b.Trailing || a.Indent != b.Indent ||
			a.OpenTrailing != b.OpenTrailing || a.CloseIndent != b.CloseIndent ||
			!rawChildrenMatch(a.Items, b.Items) {
			return false
		}
	}
	return true
}

// parseFragment reads one construct written in the given context.
func parseFragment(src string, ctx context) (*Item, error) {
	tokens, err := tokenize(src)
	if err != nil {
		return nil, err
	}
	p := &parser{src: src, toks: tokens, lines: lineStarts(src)}
	item, err := p.construct(ctx)
	if err != nil {
		return nil, err
	}
	if p.pos != len(p.toks) {
		return nil, p.errorf("text after the construct")
	}
	return item, nil
}
