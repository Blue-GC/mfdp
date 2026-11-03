package main

import (
	"fmt"
	"strings"
)

type Shape interface {
	Area() float64
}

type Rect struct {
	W, H float64
}

func (r Rect) Area() float64 { return r.W * r.H }

func Describe(s Shape) string {
	return fmt.Sprintf("%s %.2f", strings.ToUpper("shape"), s.Area())
}

const Version = "1.0"
