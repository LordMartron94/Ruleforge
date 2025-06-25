package scanning

type ScannerInterface interface {
	Peek(n int) ([]rune, error)
	Consume(n int) ([]rune, error)
	Pushback(n int) error
	Reset()
	Current() rune
	Position() int
}

type PeekInterface interface {
	Peek(n int) ([]rune, error)
	Current() rune
	Position() int
}
