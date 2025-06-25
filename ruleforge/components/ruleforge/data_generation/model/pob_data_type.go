package model

// POBDataType is a generic constraint that lists all concrete structs
// that can be treated as an ItemInterface via their pointers.
type POBDataType interface {
	ItemBase | Essence | Gem

	GetBaseType() string
	GetVariantID() string
}
