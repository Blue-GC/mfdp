import ast
import builtins
import io
import json
import sys
import tokenize

MAX_VALUE_TEXT = 80


def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=True))
    sys.stdout.flush()


def unparse(node):
    try:
        return ast.unparse(node)
    except Exception:
        return ""


def truncate(text, limit):
    text = " ".join(text.split())
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def dotted(node):
    parts = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if isinstance(node, ast.Name):
        parts.append(node.id)
        parts.reverse()
        return ".".join(parts)
    return None


def child_blocks(stmt):
    blocks = []
    if isinstance(stmt, (ast.If, ast.For, ast.AsyncFor, ast.While)):
        blocks.append(stmt.body)
        blocks.append(stmt.orelse)
    elif isinstance(stmt, (ast.With, ast.AsyncWith)):
        blocks.append(stmt.body)
    elif isinstance(stmt, ast.Try) or stmt.__class__.__name__ == "TryStar":
        blocks.append(stmt.body)
        for handler in stmt.handlers:
            blocks.append(handler.body)
        blocks.append(stmt.orelse)
        blocks.append(stmt.finalbody)
    elif stmt.__class__.__name__ == "Match":
        for case in stmt.cases:
            blocks.append(case.body)
    return blocks


def target_names(target):
    if isinstance(target, ast.Name):
        return [target.id]
    if isinstance(target, (ast.Tuple, ast.List)):
        names = []
        for element in target.elts:
            names.extend(target_names(element))
        return names
    if isinstance(target, ast.Starred):
        return target_names(target.value)
    return []


def format_arguments(args):
    params = []
    texts = []
    positional = list(args.posonlyargs) + list(args.args)
    defaults = [None] * (len(positional) - len(args.defaults)) + list(args.defaults)

    def add(name, annotation, default):
        ann = unparse(annotation) if annotation is not None else ""
        dflt = unparse(default) if default is not None else ""
        params.append({"name": name, "annotation": ann, "default": dflt})
        text = name
        if ann:
            text += ": " + ann
        if dflt:
            text += (" = " if ann else "=") + dflt
        texts.append(text)

    for index, arg in enumerate(positional):
        add(arg.arg, arg.annotation, defaults[index])
        if args.posonlyargs and index == len(args.posonlyargs) - 1:
            texts.append("/")
    if args.vararg is not None:
        add("*" + args.vararg.arg, args.vararg.annotation, None)
    elif args.kwonlyargs:
        texts.append("*")
    for index, arg in enumerate(args.kwonlyargs):
        add(arg.arg, arg.annotation, args.kw_defaults[index])
    if args.kwarg is not None:
        add("**" + args.kwarg.arg, args.kwarg.annotation, None)
    return params, texts


def type_params_text(node):
    items = getattr(node, "type_params", None)
    if not items:
        return ""
    return "[" + ", ".join(unparse(item) for item in items) + "]"


class Analyzer:
    def __init__(self, tree, lines):
        self.tree = tree
        self.lines = lines
        self.symbols = []
        self.imports = []
        self.exports = None
        self.module_names = set()
        self.builtin_names = set(dir(builtins))
        self.collect_module_names(tree.body)
        self.find_exports(tree.body)

    def collect_module_names(self, body):
        for stmt in body:
            if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                self.module_names.add(stmt.name)
            elif isinstance(stmt, ast.Assign):
                for target in stmt.targets:
                    self.module_names.update(target_names(target))
            elif isinstance(stmt, (ast.AnnAssign, ast.AugAssign)):
                self.module_names.update(target_names(stmt.target))
            elif isinstance(stmt, (ast.Import, ast.ImportFrom)):
                for alias in stmt.names:
                    name = alias.asname or alias.name.split(".")[0]
                    self.module_names.add(name)
            for block in child_blocks(stmt):
                self.collect_module_names(block)

    def find_exports(self, body):
        for stmt in body:
            names = None
            if isinstance(stmt, ast.Assign):
                if any(isinstance(t, ast.Name) and t.id == "__all__" for t in stmt.targets):
                    names = stmt.value
            elif isinstance(stmt, ast.AnnAssign):
                if isinstance(stmt.target, ast.Name) and stmt.target.id == "__all__" and stmt.value is not None:
                    names = stmt.value
            elif isinstance(stmt, ast.AugAssign):
                if isinstance(stmt.target, ast.Name) and stmt.target.id == "__all__":
                    names = stmt.value
            if names is not None and isinstance(names, (ast.List, ast.Tuple)):
                if self.exports is None:
                    self.exports = []
                for element in names.elts:
                    if isinstance(element, ast.Constant) and isinstance(element.value, str):
                        self.exports.append(element.value)

    def exported_flag(self, name, parent):
        if parent >= 0 or self.exports is None:
            return "unknown"
        return "yes" if name in self.exports else "no"

    def fragment(self, start, end):
        return "\n".join(self.lines[start - 1 : end])

    def gather(self, nodes, self_name, skip_defs):
        stack = list(reversed(nodes))
        local_names = set()
        declared_global = set()
        loads = []
        calls = []
        while stack:
            node = stack.pop()
            if skip_defs and isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                continue
            if isinstance(node, ast.Name):
                if isinstance(node.ctx, (ast.Store, ast.Del)):
                    local_names.add(node.id)
                else:
                    loads.append(node.id)
            elif isinstance(node, ast.arg):
                local_names.add(node.arg)
            elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                local_names.add(node.name)
            elif isinstance(node, (ast.Import, ast.ImportFrom)):
                for alias in node.names:
                    local_names.add((alias.asname or alias.name).split(".")[0])
            elif isinstance(node, ast.ExceptHandler):
                if node.name:
                    local_names.add(node.name)
            elif isinstance(node, (ast.Global, ast.Nonlocal)):
                declared_global.update(node.names)
            elif isinstance(node, ast.Call):
                name = dotted(node.func)
                if name:
                    calls.append(name)
            elif node.__class__.__name__ in ("MatchAs", "MatchStar"):
                if getattr(node, "name", None):
                    local_names.add(node.name)
            elif node.__class__.__name__ == "MatchMapping":
                if getattr(node, "rest", None):
                    local_names.add(node.rest)
            stack.extend(reversed(list(ast.iter_child_nodes(node))))
        local_names -= declared_global

        def keep(name):
            root = name.split(".")[0]
            if self_name is not None and root == self_name:
                return True
            if root in local_names:
                return False
            if root in self.builtin_names and root not in self.module_names:
                return False
            return True

        def normalize(name):
            parts = name.split(".")
            if self_name is not None and parts[0] == self_name:
                parts[0] = "this"
            return ".".join(parts)

        out_calls = []
        for name in calls:
            if keep(name):
                value = normalize(name)
                if value not in out_calls:
                    out_calls.append(value)
        out_refs = []
        for name in loads:
            if self_name is not None and name == self_name:
                continue
            if keep(name) and name not in out_refs:
                out_refs.append(name)
        return out_calls, out_refs

    def add_symbol(self, kind, name, parent, signature, start, end, node_doc, params, returns,
                   decorators, bases, calls, refs, fragment_text):
        qualified = name if parent < 0 else self.symbols[parent]["qualified"] + "." + name
        symbol = {
            "kind": kind,
            "parent": parent,
            "name": name,
            "qualified": qualified,
            "signature": signature,
            "params": params,
            "returns": returns,
            "decorators": decorators,
            "bases": bases,
            "doc": node_doc,
            "startLine": start,
            "endLine": end,
            "exported": self.exported_flag(name, parent),
            "calls": calls,
            "refs": refs,
            "source": fragment_text,
            "defaultExport": False,
        }
        self.symbols.append(symbol)
        return len(self.symbols) - 1

    def visit_block(self, body, parent, scope, self_name):
        for stmt in body:
            self.visit_stmt(stmt, parent, scope, self_name)

    def visit_stmt(self, stmt, parent, scope, self_name):
        if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef)):
            self.add_function(stmt, parent, scope, self_name)
            return
        if isinstance(stmt, ast.ClassDef):
            self.add_class(stmt, parent, scope, self_name)
            return
        if scope != "function":
            if isinstance(stmt, (ast.Import, ast.ImportFrom)):
                self.add_import(stmt)
                return
            if isinstance(stmt, (ast.Assign, ast.AnnAssign)):
                self.add_variables(stmt, parent, scope)
                return
            if stmt.__class__.__name__ == "TypeAlias":
                self.add_type_alias(stmt, parent)
                return
        for block in child_blocks(stmt):
            self.visit_block(block, parent, scope, self_name)

    def add_import(self, stmt):
        start = stmt.lineno
        end = stmt.end_lineno or stmt.lineno
        text = self.fragment(start, end)
        entry = {"line": start, "endLine": end, "text": text, "module": "", "star": False, "bindings": []}
        if isinstance(stmt, ast.Import):
            for alias in stmt.names:
                if alias.asname:
                    local = alias.asname
                    depth = 1
                else:
                    local = alias.name.split(".")[0]
                    depth = len(alias.name.split("."))
                entry["bindings"].append(
                    {"local": local, "imported": "*", "module": alias.name, "depth": depth}
                )
            if stmt.names:
                entry["module"] = stmt.names[0].name
        else:
            module = "." * (stmt.level or 0) + (stmt.module or "")
            entry["module"] = module
            for alias in stmt.names:
                if alias.name == "*":
                    entry["star"] = True
                else:
                    entry["bindings"].append(
                        {
                            "local": alias.asname or alias.name,
                            "imported": alias.name,
                            "module": module,
                            "depth": 1,
                        }
                    )
        self.imports.append(entry)

    def add_variables(self, stmt, parent, scope):
        if isinstance(stmt, ast.Assign):
            names = []
            for target in stmt.targets:
                names.extend(target_names(target))
            annotation = None
        else:
            names = target_names(stmt.target)
            annotation = stmt.annotation
        if not names or stmt.value is None and annotation is None:
            return
        start = stmt.lineno
        end = stmt.end_lineno or stmt.lineno
        value_text = truncate(unparse(stmt.value), MAX_VALUE_TEXT) if stmt.value is not None else ""
        ann_text = unparse(annotation) if annotation is not None else ""
        nodes = []
        if stmt.value is not None:
            nodes.append(stmt.value)
        if annotation is not None:
            nodes.append(annotation)
        calls, refs = self.gather(nodes, None, False)
        text = self.fragment(start, end)
        kind = "property" if scope == "class" else "variable"
        for name in names:
            signature = name
            if ann_text:
                signature += ": " + ann_text
            if value_text:
                signature += " = " + value_text
            self.add_symbol(kind, name, parent, signature, start, end, "", [], ann_text, [], [],
                            calls, refs, text)

    def add_type_alias(self, stmt, parent):
        name = stmt.name.id
        start = stmt.lineno
        end = stmt.end_lineno or stmt.lineno
        value_text = truncate(unparse(stmt.value), MAX_VALUE_TEXT * 2)
        signature = "type " + name + type_params_text(stmt) + " = " + value_text
        calls, refs = self.gather([stmt.value], None, False)
        self.add_symbol("type", name, parent, signature, start, end, "", [], "", [], [], calls, refs,
                        self.fragment(start, end))

    def add_function(self, node, parent, scope, self_name):
        decorators = [unparse(d) for d in node.decorator_list]
        start = min([node.lineno] + [d.lineno for d in node.decorator_list])
        end = node.end_lineno or node.lineno
        params, texts = format_arguments(node.args)
        returns = unparse(node.returns) if node.returns is not None else ""
        prefix = "async def " if isinstance(node, ast.AsyncFunctionDef) else "def "
        signature = prefix + node.name + type_params_text(node) + "(" + ", ".join(texts) + ")"
        if returns:
            signature += " -> " + returns
        kind = "function"
        local_self = self_name
        if scope == "class":
            kind = "constructor" if node.name == "__init__" else "method"
            first = list(node.args.posonlyargs) + list(node.args.args)
            is_static = any(
                (dotted(d) or "").split(".")[-1] == "staticmethod" for d in node.decorator_list
            )
            local_self = first[0].arg if first and not is_static else None
        nodes = list(node.decorator_list) + [node.args]
        if node.returns is not None:
            nodes.append(node.returns)
        nodes.extend(node.body)
        calls, refs = self.gather(nodes, local_self, False)
        doc = ast.get_docstring(node) or ""
        index = self.add_symbol(kind, node.name, parent, signature, start, end, doc, params, returns,
                                decorators, [], calls, refs, self.fragment(start, end))
        self.visit_block(node.body, index, "function", local_self)

    def add_class(self, node, parent, scope, self_name):
        decorators = [unparse(d) for d in node.decorator_list]
        start = min([node.lineno] + [d.lineno for d in node.decorator_list])
        end = node.end_lineno or node.lineno
        parts = [unparse(b) for b in node.bases]
        for keyword in node.keywords:
            if keyword.arg is None:
                parts.append("**" + unparse(keyword.value))
            else:
                parts.append(keyword.arg + "=" + unparse(keyword.value))
        signature = "class " + node.name + type_params_text(node)
        if parts:
            signature += "(" + ", ".join(parts) + ")"
        bases = []
        for base in node.bases:
            target = base.value if isinstance(base, ast.Subscript) else base
            name = dotted(target)
            if name and name not in bases:
                bases.append(name)
        nodes = list(node.decorator_list) + list(node.bases) + [k.value for k in node.keywords]
        nodes.extend(node.body)
        calls, refs = self.gather(nodes, None, True)
        doc = ast.get_docstring(node) or ""
        index = self.add_symbol("class", node.name, parent, signature, start, end, doc, [], "",
                                decorators, bases, calls, refs, self.fragment(start, end))
        self.visit_block(node.body, index, "class", None)


def decode_source(data):
    encoding, _ = tokenize.detect_encoding(io.BytesIO(data).readline)
    text = data.decode(encoding)
    if text.startswith("\ufeff"):
        text = text[1:]
    return text.replace("\r\n", "\n").replace("\r", "\n")


def analyze(data, filename):
    tree = ast.parse(data, filename=filename)
    text = decode_source(data)
    analyzer = Analyzer(tree, text.split("\n"))
    analyzer.visit_block(tree.body, -1, "module", None)
    return {
        "ok": True,
        "language": "python",
        "imports": analyzer.imports,
        "symbols": analyzer.symbols,
        "exports": analyzer.exports,
    }


def failure(message, line=0, column=0, kind="parse_error"):
    return {"ok": False, "error": {"kind": kind, "message": message, "line": line, "column": column}}


def main():
    filename = "<source>"
    args = sys.argv[1:]
    index = 0
    while index < len(args):
        if args[index] == "--filename" and index + 1 < len(args):
            filename = args[index + 1]
            index += 2
        else:
            index += 1
    data = sys.stdin.buffer.read()
    try:
        emit(analyze(data, filename))
    except SyntaxError as error:
        emit(failure(error.msg or "invalid syntax", error.lineno or 0, error.offset or 0))
    except (UnicodeDecodeError, LookupError) as error:
        emit(failure("cannot decode source: " + str(error)))
    except RecursionError:
        emit(failure("source nesting is too deep to parse"))
    except (ValueError, MemoryError) as error:
        emit(failure("cannot parse source: " + str(error)))


main()
