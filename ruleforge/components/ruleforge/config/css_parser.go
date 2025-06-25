package config

import (
	"bytes"
	"github.com/tdewolff/parse/v2"
	"github.com/tdewolff/parse/v2/css"
	"io"
	"os"
	"strings"
)

// CSSParser is a CSS parser specifically for extracting custom properties (variables).
type CSSParser struct {
	p *css.Parser
}

// NewCSSParser creates a new CSSParser from an io.Reader.
func NewCSSParser(r io.Reader) *CSSParser {
	return &CSSParser{
		p: css.NewParser(parse.NewInput(r), false),
	}
}

// NewCSSParserFromFile opens the CSS file at path, reads its contents,
// and returns a CSSParser ready to extract custom properties.
func NewCSSParserFromFile(path string) (*CSSParser, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	return &CSSParser{
		p: css.NewParser(parse.NewInput(bytes.NewReader(data)), false),
	}, nil
}

func (p *CSSParser) Parse() (map[string]string, error) {
	props := make(map[string]string)
	for {
		key, val, err := p.nextProperty()
		if err == io.EOF {
			return props, nil
		}
		if err != nil {
			return nil, err
		}
		props[key] = val
	}
}

func (p *CSSParser) nextProperty() (key, val string, err error) {
	for {
		gt, _, data := p.p.Next()

		if gt == css.ErrorGrammar {
			return "", "", p.p.Err()
		}

		if !isCustomProperty(gt, data) {
			continue
		}

		key, val := extractProperty(p, data)
		return key, val, nil
	}
}

func isCustomProperty(gt css.GrammarType, data []byte) bool {
	return gt == css.CustomPropertyGrammar ||
		(gt == css.DeclarationGrammar && bytes.HasPrefix(data, []byte("--")))
}

func extractProperty(p *CSSParser, data []byte) (string, string) {
	key := strings.TrimSpace(string(data))
	key = strings.Replace(key, "--", "", -1)

	var sb strings.Builder
	for _, v := range p.p.Values() {
		sb.Write(v.Data)
	}
	val := strings.TrimSpace(sb.String())
	return key, val
}
