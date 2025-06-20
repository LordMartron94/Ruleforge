package model

type Unique struct {
	Name      string
	BaseType  string
	Variants  []string
	League    string
	Source    string
	LevelReq  int
	Implicits int
	Modifiers []string
}
