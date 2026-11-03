package main

import (
	"encoding/json"
	"go/ast"
	"go/parser"
	"go/scanner"
	"go/token"
	"io"
	"os"
	"regexp"
	"strings"
)

type Param struct {
	Name       string `json:"name"`
	Annotation string `json:"annotation"`
	Default    string `json:"default"`
}

type Symbol struct {
	Kind          string   `json:"kind"`
	Parent        int      `json:"parent"`
	Name          string   `json:"name"`
	Qualified     string   `json:"qualified"`
	Signature     string   `json:"signature"`
	Params        []Param  `json:"params"`
	Returns       string   `json:"returns"`
	Decorators    []string `json:"decorators"`
	Bases         []string `json:"bases"`
	Doc           string   `json:"doc"`
	StartLine     int      `json:"startLine"`
	EndLine       int      `json:"endLine"`
	Exported      string   `json:"exported"`
	Calls         []string `json:"calls"`
	Refs          []string `json:"refs"`
	Source        string   `json:"source"`
	DefaultExport bool     `json:"defaultExport"`
}

type Binding struct {
	Local    string `json:"local"`
	Imported string `json:"imported"`
	Module   string `json:"module"`
	Depth    int    `json:"depth"`
}

type Import struct {
	Line     int       `json:"line"`
	EndLine  int       `json:"endLine"`
	Text     string    `json:"text"`
	Module   string    `json:"module"`
	Star     bool      `json:"star"`
	Bindings []Binding `json:"bindings"`
}

type ErrInfo struct {
	Kind    string `json:"kind"`
	Message string `json:"message"`
	Line    int    `json:"line"`
	Column  int    `json:"column"`
}

type Output struct {
	Ok       bool     `json:"ok"`
	Language string   `json:"language,omitempty"`
	Imports  []Import `json:"imports"`
	Symbols  []Symbol `json:"symbols"`
	Exports  []string `json:"exports"`
	Error    *ErrInfo `json:"error,omitempty"`
}

var universe = map[string]bool{}

var versionSuffix = regexp.MustCompile(`^v[0-9]+$`)

func init() {
	names := []string{
		"any", "bool", "byte", "comparable", "complex64", "complex128", "error", "float32", "float64",
		"int", "int8", "int16", "int32", "int64", "rune", "string", "uint", "uint8", "uint16", "uint32",
		"uint64", "uintptr", "true", "false", "iota", "nil", "append", "cap", "clear", "close",
		"complex", "copy", "delete", "imag", "len", "make", "max", "min", "new", "panic", "print",
		"println", "real", "recover",
	}
	for _, n := range names {
		universe[n] = true
	}
}

type analyzer struct {
	fset      *token.FileSet
	src       []byte
	symbols   []Symbol
	imports   []Import
	exports   []string
	typeIndex map[string]int
}

func collapse(s string) string {
	return strings.Join(strings.Fields(s), " ")
}

func truncate(s string, limit int) string {
	s = collapse(s)
	if len(s) <= limit {
		return s
	}
	return s[:limit-3] + "..."
}

func (a *analyzer) off(p token.Pos) int {
	return a.fset.Position(p).Offset
}

func (a *analyzer) slice(from, to token.Pos) string {
	start := a.off(from)
	end := a.off(to)
	if start < 0 || end > len(a.src) || end < start {
		return ""
	}
	return string(a.src[start:end])
}

func (a *analyzer) text(n ast.Node) string {
	return a.slice(n.Pos(), n.End())
}

func (a *analyzer) startLine(n ast.Node) int {
	return a.fset.Position(n.Pos()).Line
}

func (a *analyzer) endLine(n ast.Node) int {
	return a.fset.Position(n.End() - 1).Line
}

func docText(groups ...*ast.CommentGroup) string {
	for _, g := range groups {
		if g != nil {
			t := strings.TrimSpace(g.Text())
			if t != "" {
				return t
			}
		}
	}
	return ""
}

func yesNo(name string) string {
	if ast.IsExported(name) {
		return "yes"
	}
	return "no"
}

func (a *analyzer) add(s Symbol) int {
	if s.Params == nil {
		s.Params = []Param{}
	}
	if s.Decorators == nil {
		s.Decorators = []string{}
	}
	if s.Bases == nil {
		s.Bases = []string{}
	}
	if s.Calls == nil {
		s.Calls = []string{}
	}
	if s.Refs == nil {
		s.Refs = []string{}
	}
	a.symbols = append(a.symbols, s)
	return len(a.symbols) - 1
}

func dotted(e ast.Expr) string {
	switch x := e.(type) {
	case *ast.Ident:
		return x.Name
	case *ast.SelectorExpr:
		base := dotted(x.X)
		if base == "" {
			return ""
		}
		return base + "." + x.Sel.Name
	case *ast.ParenExpr:
		return dotted(x.X)
	case *ast.IndexExpr:
		return dotted(x.X)
	case *ast.IndexListExpr:
		return dotted(x.X)
	case *ast.StarExpr:
		return dotted(x.X)
	}
	return ""
}

func receiverTypeName(e ast.Expr) string {
	return dotted(e)
}

func finish(names []string, locals map[string]bool, recv string) []string {
	out := []string{}
	seen := map[string]bool{}
	for _, n := range names {
		root := n
		rest := ""
		if i := strings.Index(n, "."); i >= 0 {
			root = n[:i]
			rest = n[i:]
		}
		if recv != "" && root == recv {
			n = "this" + rest
		} else if locals[root] || universe[root] {
			continue
		}
		if n == "this" {
			continue
		}
		if !seen[n] {
			seen[n] = true
			out = append(out, n)
		}
	}
	return out
}

func gather(roots []ast.Node, recv string) ([]string, []string) {
	locals := map[string]bool{}
	skip := map[*ast.Ident]bool{}
	for _, r := range roots {
		ast.Inspect(r, func(n ast.Node) bool {
			switch x := n.(type) {
			case *ast.Field:
				for _, id := range x.Names {
					locals[id.Name] = true
					skip[id] = true
				}
			case *ast.AssignStmt:
				if x.Tok == token.DEFINE {
					for _, l := range x.Lhs {
						if id, ok := l.(*ast.Ident); ok {
							locals[id.Name] = true
							skip[id] = true
						}
					}
				}
			case *ast.RangeStmt:
				if x.Tok == token.DEFINE {
					if id, ok := x.Key.(*ast.Ident); ok {
						locals[id.Name] = true
						skip[id] = true
					}
					if id, ok := x.Value.(*ast.Ident); ok {
						locals[id.Name] = true
						skip[id] = true
					}
				}
			case *ast.ValueSpec:
				for _, id := range x.Names {
					locals[id.Name] = true
					skip[id] = true
				}
			case *ast.TypeSpec:
				locals[x.Name.Name] = true
				skip[x.Name] = true
			case *ast.TypeSwitchStmt:
				if as, ok := x.Assign.(*ast.AssignStmt); ok {
					for _, l := range as.Lhs {
						if id, ok := l.(*ast.Ident); ok {
							locals[id.Name] = true
							skip[id] = true
						}
					}
				}
			case *ast.LabeledStmt:
				skip[x.Label] = true
			case *ast.BranchStmt:
				if x.Label != nil {
					skip[x.Label] = true
				}
			case *ast.KeyValueExpr:
				if id, ok := x.Key.(*ast.Ident); ok {
					skip[id] = true
				}
			case *ast.SelectorExpr:
				skip[x.Sel] = true
			}
			return true
		})
	}
	if recv != "" {
		delete(locals, recv)
	}
	var calls, loads []string
	for _, r := range roots {
		ast.Inspect(r, func(n ast.Node) bool {
			switch x := n.(type) {
			case *ast.CallExpr:
				if name := dotted(x.Fun); name != "" {
					calls = append(calls, name)
				}
			case *ast.Ident:
				if !skip[x] && x.Name != "_" {
					loads = append(loads, x.Name)
				}
			}
			return true
		})
	}
	return finish(calls, locals, recv), finish(loads, locals, recv)
}

func (a *analyzer) paramsOf(fl *ast.FieldList) []Param {
	out := []Param{}
	if fl == nil {
		return out
	}
	for _, f := range fl.List {
		ann := collapse(a.text(f.Type))
		if len(f.Names) == 0 {
			out = append(out, Param{Name: "", Annotation: ann})
			continue
		}
		for _, n := range f.Names {
			out = append(out, Param{Name: n.Name, Annotation: ann})
		}
	}
	return out
}

func (a *analyzer) resultsText(ft *ast.FuncType) string {
	if ft.Results == nil || len(ft.Results.List) == 0 {
		return ""
	}
	if ft.Results.Opening.IsValid() {
		return collapse(a.slice(ft.Results.Opening, ft.Results.Closing+1))
	}
	return collapse(a.text(ft.Results.List[0].Type))
}

func (a *analyzer) typeParamsText(fl *ast.FieldList) string {
	if fl == nil || !fl.Opening.IsValid() {
		return ""
	}
	return "[" + collapse(a.slice(fl.Opening+1, fl.Closing)) + "]"
}

func (a *analyzer) addImports(f *ast.File) {
	for _, spec := range f.Imports {
		path := strings.Trim(spec.Path.Value, "\"`")
		local := ""
		star := false
		text := "import " + collapse(a.text(spec))
		if spec.Name != nil {
			local = spec.Name.Name
			if local == "." {
				star = true
			}
		} else {
			parts := strings.Split(path, "/")
			local = parts[len(parts)-1]
			if versionSuffix.MatchString(local) && len(parts) > 1 {
				local = parts[len(parts)-2]
			}
			if i := strings.Index(local, ".v"); i > 0 {
				local = local[:i]
			}
			local = strings.ReplaceAll(local, "-", "_")
		}
		imp := Import{
			Line:     a.startLine(spec),
			EndLine:  a.endLine(spec),
			Text:     text,
			Module:   path,
			Star:     star,
			Bindings: []Binding{},
		}
		if local != "_" && local != "." {
			imp.Bindings = append(imp.Bindings, Binding{Local: local, Imported: "*", Module: path, Depth: 1})
		}
		a.imports = append(a.imports, imp)
	}
}

func embeddedName(e ast.Expr) string {
	return dotted(e)
}

func (a *analyzer) addType(gd *ast.GenDecl, spec *ast.TypeSpec) {
	name := spec.Name.Name
	kind := "type"
	signature := "type " + name + a.typeParamsText(spec.TypeParams)
	switch spec.Type.(type) {
	case *ast.StructType:
		kind = "class"
		signature += " struct"
	case *ast.InterfaceType:
		kind = "interface"
		signature += " interface"
	default:
		if spec.Assign.IsValid() {
			signature += " = "
		} else {
			signature += " "
		}
		signature += collapse(a.text(spec.Type))
	}
	var bases []string
	switch t := spec.Type.(type) {
	case *ast.StructType:
		if t.Fields != nil {
			for _, f := range t.Fields.List {
				if len(f.Names) == 0 {
					if b := embeddedName(f.Type); b != "" {
						bases = append(bases, b)
					}
				}
			}
		}
	case *ast.InterfaceType:
		if t.Methods != nil {
			for _, m := range t.Methods.List {
				if len(m.Names) == 0 {
					if b := embeddedName(m.Type); b != "" {
						bases = append(bases, b)
					}
				}
			}
		}
	}
	var node ast.Node = spec
	source := "type " + a.text(spec)
	if !gd.Lparen.IsValid() && len(gd.Specs) == 1 {
		node = gd
		source = a.text(gd)
	}
	roots := []ast.Node{spec.Type}
	if spec.TypeParams != nil {
		roots = append(roots, spec.TypeParams)
	}
	calls, refs := gather(roots, "")
	idx := a.add(Symbol{
		Kind:      kind,
		Parent:    -1,
		Name:      name,
		Qualified: name,
		Signature: truncate(signature, 240),
		Bases:     bases,
		Doc:       docText(spec.Doc, gd.Doc),
		StartLine: a.startLine(node),
		EndLine:   a.endLine(node),
		Exported:  yesNo(name),
		Calls:     calls,
		Refs:      refs,
		Source:    source,
	})
	a.typeIndex[name] = idx
	a.exports = appendExport(a.exports, name)
	switch t := spec.Type.(type) {
	case *ast.StructType:
		if t.Fields == nil {
			return
		}
		for _, f := range t.Fields.List {
			if len(f.Names) == 0 {
				continue
			}
			c, r := gather([]ast.Node{f.Type}, "")
			for _, id := range f.Names {
				a.add(Symbol{
					Kind:      "property",
					Parent:    idx,
					Name:      id.Name,
					Qualified: name + "." + id.Name,
					Signature: truncate(id.Name+" "+a.text(f.Type), 240),
					Returns:   collapse(a.text(f.Type)),
					Doc:       docText(f.Doc),
					StartLine: a.startLine(f),
					EndLine:   a.endLine(f),
					Exported:  "unknown",
					Calls:     c,
					Refs:      r,
					Source:    a.text(f),
				})
			}
		}
	case *ast.InterfaceType:
		if t.Methods == nil {
			return
		}
		for _, m := range t.Methods.List {
			ft, ok := m.Type.(*ast.FuncType)
			if !ok || len(m.Names) == 0 {
				continue
			}
			c, r := gather([]ast.Node{ft}, "")
			for _, id := range m.Names {
				a.add(Symbol{
					Kind:      "method",
					Parent:    idx,
					Name:      id.Name,
					Qualified: name + "." + id.Name,
					Signature: truncate(id.Name+a.text(ft), 240),
					Params:    a.paramsOf(ft.Params),
					Returns:   a.resultsText(ft),
					Doc:       docText(m.Doc),
					StartLine: a.startLine(m),
					EndLine:   a.endLine(m),
					Exported:  "unknown",
					Calls:     c,
					Refs:      r,
					Source:    a.text(m),
				})
			}
		}
	}
}

func appendExport(list []string, name string) []string {
	if !ast.IsExported(name) {
		return list
	}
	return append(list, name)
}

func (a *analyzer) addValues(gd *ast.GenDecl) {
	single := !gd.Lparen.IsValid() && len(gd.Specs) == 1
	for _, s := range gd.Specs {
		vs, ok := s.(*ast.ValueSpec)
		if !ok {
			continue
		}
		for i, id := range vs.Names {
			if id.Name == "_" {
				continue
			}
			kind := "variable"
			if gd.Tok == token.CONST {
				kind = "constant"
			}
			signature := gd.Tok.String() + " " + id.Name
			var roots []ast.Node
			if vs.Type != nil {
				signature += " " + collapse(a.text(vs.Type))
				roots = append(roots, vs.Type)
			}
			if i < len(vs.Values) {
				signature += " = " + truncate(a.text(vs.Values[i]), 60)
				roots = append(roots, vs.Values[i])
			}
			var node ast.Node = vs
			source := gd.Tok.String() + " " + a.text(vs)
			if single {
				node = gd
				source = a.text(gd)
			}
			calls, refs := gather(roots, "")
			a.add(Symbol{
				Kind:      kind,
				Parent:    -1,
				Name:      id.Name,
				Qualified: id.Name,
				Signature: collapse(signature),
				Returns:   collapse(a.textOrEmpty(vs.Type)),
				Doc:       docText(vs.Doc, gd.Doc),
				StartLine: a.startLine(node),
				EndLine:   a.endLine(node),
				Exported:  yesNo(id.Name),
				Calls:     calls,
				Refs:      refs,
				Source:    source,
			})
			a.exports = appendExport(a.exports, id.Name)
		}
	}
}

func (a *analyzer) textOrEmpty(e ast.Expr) string {
	if e == nil {
		return ""
	}
	return a.text(e)
}

func (a *analyzer) addFunc(fd *ast.FuncDecl) {
	name := fd.Name.Name
	kind := "function"
	parent := -1
	qualified := name
	recv := ""
	exported := yesNo(name)
	var roots []ast.Node
	if fd.Recv != nil && len(fd.Recv.List) > 0 {
		kind = "method"
		exported = "unknown"
		r := fd.Recv.List[0]
		if len(r.Names) > 0 && r.Names[0].Name != "_" {
			recv = r.Names[0].Name
		}
		if tn := receiverTypeName(r.Type); tn != "" {
			qualified = tn + "." + name
			if idx, ok := a.typeIndex[tn]; ok {
				parent = idx
			}
		}
		roots = append(roots, fd.Recv)
	}
	roots = append(roots, fd.Type)
	end := fd.End()
	if fd.Body != nil {
		end = fd.Body.Lbrace
		roots = append(roots, fd.Body)
	}
	calls, refs := gather(roots, recv)
	if kind == "function" {
		a.exports = appendExport(a.exports, name)
	}
	a.add(Symbol{
		Kind:      kind,
		Parent:    parent,
		Name:      name,
		Qualified: qualified,
		Signature: collapse(a.slice(fd.Pos(), end)),
		Params:    a.paramsOf(fd.Type.Params),
		Returns:   a.resultsText(fd.Type),
		Doc:       docText(fd.Doc),
		StartLine: a.startLine(fd),
		EndLine:   a.endLine(fd),
		Exported:  exported,
		Calls:     calls,
		Refs:      refs,
		Source:    a.text(fd),
	})
}

func analyze(filename string, src []byte) Output {
	fset := token.NewFileSet()
	file, err := parser.ParseFile(fset, filename, src, parser.ParseComments)
	if err != nil {
		line, col, msg := 0, 0, err.Error()
		if list, ok := err.(scanner.ErrorList); ok && len(list) > 0 {
			line, col, msg = list[0].Pos.Line, list[0].Pos.Column, list[0].Msg
		}
		return Output{Ok: false, Imports: []Import{}, Symbols: []Symbol{}, Exports: []string{},
			Error: &ErrInfo{Kind: "parse_error", Message: msg, Line: line, Column: col}}
	}
	a := &analyzer{fset: fset, src: src, typeIndex: map[string]int{}}
	a.addImports(file)
	for _, d := range file.Decls {
		if gd, ok := d.(*ast.GenDecl); ok && gd.Tok == token.TYPE {
			for _, s := range gd.Specs {
				if ts, ok := s.(*ast.TypeSpec); ok {
					a.addType(gd, ts)
				}
			}
		}
	}
	for _, d := range file.Decls {
		switch x := d.(type) {
		case *ast.FuncDecl:
			a.addFunc(x)
		case *ast.GenDecl:
			if x.Tok == token.CONST || x.Tok == token.VAR {
				a.addValues(x)
			}
		}
	}
	if a.imports == nil {
		a.imports = []Import{}
	}
	if a.symbols == nil {
		a.symbols = []Symbol{}
	}
	if a.exports == nil {
		a.exports = []string{}
	}
	return Output{Ok: true, Language: "go", Imports: a.imports, Symbols: a.symbols, Exports: a.exports}
}

func emit(o Output) {
	enc := json.NewEncoder(os.Stdout)
	enc.SetEscapeHTML(false)
	_ = enc.Encode(o)
}

func main() {
	filename := "source.go"
	args := os.Args[1:]
	for i := 0; i < len(args); i++ {
		if args[i] == "--filename" && i+1 < len(args) {
			filename = args[i+1]
			i++
		}
	}
	src, err := io.ReadAll(os.Stdin)
	if err != nil {
		emit(Output{Ok: false, Imports: []Import{}, Symbols: []Symbol{}, Exports: []string{},
			Error: &ErrInfo{Kind: "parse_error", Message: "cannot read source: " + err.Error()}})
		return
	}
	emit(analyze(filename, src))
}
