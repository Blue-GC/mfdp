import json
import os
import re
import sys

MAX_VALUE_TEXT = 60
MAX_SIGNATURE_TEXT = 240

IGNORABLE = (
    "file not found",
    "unknown type name",
    "undeclared identifier",
    "undeclared function",
    "use of undeclared",
    "implicit declaration",
    "no template named",
    "unknown class name",
    "no type named",
    "no member named",
    "unknown namespace",
    "incomplete type",
    "unknown type",
)


def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=True))
    sys.stdout.flush()


def failure(message, line=0, column=0, kind="parse_error"):
    return {"ok": False, "error": {"kind": kind, "message": message, "line": line, "column": column}}


def collapse(text):
    return " ".join(text.split())


def truncate(text, limit):
    text = collapse(text)
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def safe(fn, default=None):
    try:
        return fn()
    except Exception:
        return default


def kind_of(cursor):
    try:
        return cursor.kind
    except ValueError:
        return None


def load_cindex():
    try:
        from clang import cindex
    except Exception:
        return None, "python package 'libclang' not found; install it with: pip install libclang"
    library = os.environ.get("MFDP_LIBCLANG")
    if library:
        try:
            cindex.Config.set_library_file(library)
        except Exception:
            pass
    return cindex, None


def clean_doc(raw):
    if not raw:
        return ""
    lines = []
    for line in raw.strip().split("\n"):
        text = line.strip()
        if text.startswith("/**") or text.startswith("/*!"):
            text = text[3:]
        elif text.startswith("///") or text.startswith("//!"):
            text = text[3:]
        elif text.startswith("//"):
            text = text[2:]
        elif text.startswith("*/"):
            text = ""
        elif text.startswith("*"):
            text = text[1:]
        if text.endswith("*/"):
            text = text[:-2]
        lines.append(text.strip())
    return "\n".join(lines).strip()


class Analyzer:
    def __init__(self, cindex, tu, virtual, data, language):
        self.cindex = cindex
        self.K = cindex.CursorKind
        self.tu = tu
        self.virtual = virtual
        self.data = data
        self.lines = data.decode("utf-8", "replace").split("\n")
        self.cpp = language == "cpp"
        self.sep = "::" if self.cpp else "."
        self.symbols = []
        self.imports = []
        self.usr = {}
        self.defined = {}
        K = self.K

        def kinds(*names):
            return {getattr(K, n) for n in names if hasattr(K, n)}

        self.func_kinds = kinds(
            "FUNCTION_DECL", "CXX_METHOD", "CONSTRUCTOR", "DESTRUCTOR", "CONVERSION_FUNCTION",
            "FUNCTION_TEMPLATE",
        )
        self.record_kinds = kinds("CLASS_DECL", "STRUCT_DECL", "UNION_DECL", "CLASS_TEMPLATE")
        self.typedef_kinds = kinds("TYPEDEF_DECL", "TYPE_ALIAS_DECL", "TYPE_ALIAS_TEMPLATE_DECL")
        self.scope_kinds = self.record_kinds | kinds("NAMESPACE")
        self.member_kinds = (
            self.func_kinds | self.record_kinds | self.typedef_kinds
            | kinds("FIELD_DECL", "VAR_DECL", "ENUM_DECL")
        )
        self.ref_kinds = kinds("DECL_REF_EXPR", "TYPE_REF", "MEMBER_REF_EXPR", "TEMPLATE_REF")
        self.skipped_params = kinds(
            "PARM_DECL", "TEMPLATE_TYPE_PARAMETER", "TEMPLATE_NON_TYPE_PARAMETER",
            "TEMPLATE_TEMPLATE_PARAMETER",
        )

    def in_file(self, cursor):
        location = cursor.location
        return location.file is not None and location.file.name == self.virtual

    def slice_text(self, start, end):
        return self.data[start:end].decode("utf-8", "replace")

    def extent_text(self, cursor):
        return self.slice_text(cursor.extent.start.offset, cursor.extent.end.offset)

    def chain(self, node):
        parts = []
        current = node
        depth = 0
        while current is not None and depth < 32:
            k = kind_of(current)
            if k is None or k == self.K.TRANSLATION_UNIT:
                break
            if k in self.func_kinds and current is not node:
                return None
            name = current.spelling
            if name:
                parts.append(name)
            current = safe(lambda: current.semantic_parent)
            depth += 1
        parts.reverse()
        return parts

    def ref_name(self, cursor):
        ref = safe(lambda: cursor.referenced)
        if ref is None:
            spelling = cursor.spelling
            if spelling and re.match(r"^[A-Za-z_]\w*$", spelling) and kind_of(cursor) in (
                self.K.CALL_EXPR, self.K.DECL_REF_EXPR
            ):
                return spelling
            return None
        rk = kind_of(ref)
        if rk is None or rk in self.skipped_params:
            return None
        location = ref.location
        if location.file is None:
            return None
        if safe(lambda: location.is_in_system_header, False):
            return None
        parts = self.chain(ref)
        if parts is None:
            return None
        if rk == self.K.CONSTRUCTOR and len(parts) >= 2:
            parts = parts[:-1]
        if rk == self.K.DESTRUCTOR:
            return None
        return ".".join(parts) or None

    def gather(self, root, skip_members):
        calls = []
        refs = []
        stack = list(reversed(list(root.get_children())))
        while stack:
            cursor = stack.pop()
            k = kind_of(cursor)
            if k is None:
                continue
            if skip_members and k in self.member_kinds:
                continue
            if k == self.K.CALL_EXPR:
                name = self.ref_name(cursor)
                if name and name not in calls:
                    calls.append(name)
            elif k in self.ref_kinds:
                name = self.ref_name(cursor)
                if name and name not in refs:
                    refs.append(name)
            stack.extend(reversed(list(cursor.get_children())))
        return calls, refs

    def parent_index(self, cursor, default=-1):
        parent = safe(lambda: cursor.semantic_parent)
        if parent is None:
            return default
        k = kind_of(parent)
        if k is None or k == self.K.TRANSLATION_UNIT:
            return -1
        if k in self.scope_kinds:
            return self.usr.get(safe(lambda: parent.get_usr(), ""), -1)
        return -1

    def qualified(self, cursor, name):
        parts = self.chain(cursor)
        if not parts:
            return name
        return self.sep.join(parts)

    def add_symbol(self, kind, cursor, name, signature, params, returns, bases, calls, refs,
                   exported, source):
        idx = len(self.symbols)
        self.symbols.append({
            "kind": kind,
            "parent": self.parent_index(cursor),
            "name": name,
            "qualified": self.qualified(cursor, name),
            "signature": signature,
            "params": params,
            "returns": returns,
            "decorators": [],
            "bases": bases,
            "doc": clean_doc(safe(lambda: cursor.raw_comment)),
            "startLine": cursor.extent.start.line,
            "endLine": cursor.extent.end.line,
            "exported": exported,
            "calls": calls,
            "refs": refs,
            "source": source,
            "defaultExport": False,
        })
        return idx

    def handle(self, cursor):
        k = kind_of(cursor)
        if k is None or not self.in_file(cursor):
            return
        K = self.K
        if k == K.NAMESPACE:
            self.do_namespace(cursor)
        elif k in self.record_kinds:
            self.do_record(cursor)
        elif k in self.func_kinds:
            self.do_function(cursor)
        elif k == K.ENUM_DECL:
            self.do_enum(cursor)
        elif k in self.typedef_kinds:
            self.do_typedef(cursor)
        elif k == K.VAR_DECL:
            self.do_var(cursor)
        elif k == K.FIELD_DECL:
            self.do_field(cursor)
        elif k == K.MACRO_DEFINITION:
            self.do_macro(cursor)
        elif k == K.INCLUSION_DIRECTIVE:
            self.do_include(cursor)
        elif hasattr(K, "LINKAGE_SPEC") and k == K.LINKAGE_SPEC:
            for child in cursor.get_children():
                self.handle(child)

    def do_namespace(self, cursor):
        usr = safe(lambda: cursor.get_usr(), "")
        name = cursor.spelling
        enclosing = self.parent_index(cursor)
        if not name:
            if usr:
                self.usr[usr] = enclosing
            for child in cursor.get_children():
                self.handle(child)
            return
        if usr and usr in self.usr:
            for child in cursor.get_children():
                self.handle(child)
            return
        text = self.extent_text(cursor)
        brace = text.find("{")
        head = collapse(text[:brace] if brace >= 0 else text)
        idx = self.add_symbol("namespace", cursor, name, head, [], "", [], [], [], "unknown", text)
        if usr:
            self.usr[usr] = idx
        for child in cursor.get_children():
            self.handle(child)

    def do_record(self, cursor):
        if not safe(lambda: cursor.is_definition(), False):
            return
        name = cursor.spelling
        if not name or "unnamed" in name or "anonymous" in name:
            return
        text = self.extent_text(cursor)
        brace = text.find("{")
        head = truncate(text[:brace] if brace >= 0 else text, MAX_SIGNATURE_TEXT)
        bases = []
        for child in cursor.get_children():
            if kind_of(child) == self.K.CXX_BASE_SPECIFIER:
                base = child.spelling or safe(lambda: child.type.spelling, "")
                base = base.split("<")[0].replace("::", ".").strip()
                if base and base not in bases:
                    bases.append(base)
        calls, refs = self.gather(cursor, True)
        idx = self.add_symbol("class", cursor, name, head, [], "", bases, calls, refs, "unknown", text)
        usr = safe(lambda: cursor.get_usr(), "")
        if usr:
            self.usr[usr] = idx
        for child in cursor.get_children():
            self.handle(child)

    def function_parts(self, cursor):
        K = self.K
        k = kind_of(cursor)
        text = self.extent_text(cursor)
        body = None
        for child in cursor.get_children():
            if kind_of(child) == K.COMPOUND_STMT:
                body = child
        if body is not None:
            head = self.slice_text(cursor.extent.start.offset, body.extent.start.offset)
        else:
            head = text.rstrip().rstrip(";")
        params = []
        for child in cursor.get_children():
            if kind_of(child) == K.PARM_DECL:
                param_text = self.extent_text(child)
                default = ""
                if "=" in param_text:
                    default = collapse(param_text.split("=", 1)[1])
                params.append({
                    "name": child.spelling,
                    "annotation": safe(lambda: child.type.spelling, ""),
                    "default": default,
                })
        returns = ""
        if k not in (K.CONSTRUCTOR, K.DESTRUCTOR):
            returns = safe(lambda: cursor.result_type.spelling, "")
        return collapse(head), params, returns, text

    def do_function(self, cursor):
        K = self.K
        usr = safe(lambda: cursor.get_usr(), "")
        is_definition = safe(lambda: cursor.is_definition(), False)
        existing = self.usr.get(usr) if usr else None
        if existing is not None and existing >= 0 and usr in self.defined:
            if is_definition and not self.defined[usr]:
                signature, params, returns, text = self.function_parts(cursor)
                calls, refs = self.gather(cursor, False)
                symbol = self.symbols[existing]
                symbol["signature"] = signature
                symbol["params"] = params
                symbol["returns"] = returns
                symbol["calls"] = calls
                symbol["refs"] = refs
                symbol["source"] = text
                symbol["startLine"] = cursor.extent.start.line
                symbol["endLine"] = cursor.extent.end.line
                self.defined[usr] = True
            return
        k = kind_of(cursor)
        parent = safe(lambda: cursor.semantic_parent)
        parent_kind = kind_of(parent) if parent is not None else None
        in_class = parent_kind in self.record_kinds
        if k == K.CONSTRUCTOR:
            kind = "constructor"
        elif in_class or k in (K.CXX_METHOD, K.DESTRUCTOR, K.CONVERSION_FUNCTION):
            kind = "method"
        else:
            kind = "function"
        exported = "unknown"
        if not in_class and safe(lambda: cursor.storage_class == self.cindex.StorageClass.STATIC, False):
            exported = "no"
        signature, params, returns, text = self.function_parts(cursor)
        calls, refs = self.gather(cursor, False)
        name = cursor.spelling
        idx = self.add_symbol(kind, cursor, name, signature, params, returns, [], calls, refs, exported, text)
        if usr:
            self.usr[usr] = idx
            self.defined[usr] = bool(is_definition)

    def do_enum(self, cursor):
        if not safe(lambda: cursor.is_definition(), False):
            return
        name = cursor.spelling
        if not name or "unnamed" in name or "anonymous" in name:
            return
        text = self.extent_text(cursor)
        brace = text.find("{")
        head = collapse(text[:brace] if brace >= 0 else text)
        names = [c.spelling for c in cursor.get_children() if kind_of(c) == self.K.ENUM_CONSTANT_DECL]
        signature = truncate(head + " { " + ", ".join(names) + " }", MAX_SIGNATURE_TEXT)
        calls, refs = self.gather(cursor, False)
        self.add_symbol("enum", cursor, name, signature, [], "", [], calls, refs, "unknown", text)

    def do_typedef(self, cursor):
        name = cursor.spelling
        if not name:
            return
        text = self.extent_text(cursor)
        calls, refs = self.gather(cursor, False)
        underlying = safe(lambda: cursor.underlying_typedef_type.spelling, "")
        self.add_symbol("type", cursor, name, truncate(text, MAX_SIGNATURE_TEXT), [], underlying, [],
                        calls, refs, "unknown", text)

    def do_var(self, cursor):
        name = cursor.spelling
        if not name:
            return
        text = self.extent_text(cursor)
        type_text = safe(lambda: cursor.type.spelling, "")
        is_const = safe(lambda: cursor.type.is_const_qualified(), False)
        signature = collapse(type_text + " " + name)
        if "=" in text:
            signature += " = " + truncate(text.split("=", 1)[1], MAX_VALUE_TEXT)
        exported = "unknown"
        if safe(lambda: cursor.storage_class == self.cindex.StorageClass.STATIC, False):
            exported = "no"
        calls, refs = self.gather(cursor, False)
        self.add_symbol("constant" if is_const else "variable", cursor, name, signature, [], type_text,
                        [], calls, refs, exported, text)

    def do_field(self, cursor):
        name = cursor.spelling
        if not name:
            return
        text = self.extent_text(cursor)
        calls, refs = self.gather(cursor, False)
        self.add_symbol("property", cursor, name, truncate(text, MAX_SIGNATURE_TEXT), [],
                        safe(lambda: cursor.type.spelling, ""), [], calls, refs, "unknown", text)

    def do_macro(self, cursor):
        name = cursor.spelling
        if not name or name.startswith("__"):
            return
        body = self.extent_text(cursor)
        text = "#define " + body
        kind = "function" if re.match(r"^\w+\(", body) else "constant"
        self.add_symbol(kind, cursor, name, truncate(text, MAX_SIGNATURE_TEXT), [], "", [], [], [],
                        "unknown", text)

    def do_include(self, cursor):
        line = cursor.extent.start.line
        end = cursor.extent.end.line
        text = collapse(self.lines[line - 1]) if 0 < line <= len(self.lines) else ""
        self.imports.append({
            "line": line,
            "endLine": end,
            "text": text,
            "module": cursor.spelling,
            "star": True,
            "bindings": [],
        })

    def run(self):
        for cursor in self.tu.cursor.get_children():
            self.handle(cursor)
        for symbol in self.symbols:
            symbol["parent"] = symbol["parent"] if symbol["parent"] < len(self.symbols) else -1
        return {
            "ok": True,
            "language": "cpp" if self.cpp else "c",
            "imports": self.imports,
            "symbols": self.symbols,
            "exports": [],
        }


def parse_args(argv):
    language = "c"
    index = 0
    while index < len(argv):
        if argv[index] == "--language" and index + 1 < len(argv):
            language = argv[index + 1]
            index += 2
        else:
            index += 1
    return language


def analyze(cindex, data, language):
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return failure("source is not valid UTF-8")
    if text.startswith("\ufeff"):
        text = text[1:]
        data = text.encode("utf-8")
    cpp = language == "cpp"
    virtual = "source.cpp" if cpp else "source.c"
    args = ["-x", "c++" if cpp else "c", "-std=c++20" if cpp else "-std=gnu17", "-ferror-limit=0",
            "-fparse-all-comments", "-Wno-everything"]
    try:
        index = cindex.Index.create()
    except Exception as error:
        return failure("libclang could not be loaded: " + str(error), kind="adapter_unavailable")
    try:
        tu = index.parse(
            virtual,
            args=args,
            unsaved_files=[(virtual, text)],
            options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
        )
    except Exception as error:
        return failure("libclang failed to parse the source: " + str(error))
    for diagnostic in tu.diagnostics:
        if diagnostic.severity < cindex.Diagnostic.Error:
            continue
        location = diagnostic.location
        if location.file is not None and location.file.name != virtual:
            continue
        message = diagnostic.spelling
        lowered = message.lower()
        if any(pattern in lowered for pattern in IGNORABLE):
            continue
        return failure(message, location.line, location.column)
    return Analyzer(cindex, tu, virtual, data, language).run()


def main():
    language = parse_args(sys.argv[1:])
    data = sys.stdin.buffer.read()
    cindex, problem = load_cindex()
    if cindex is None:
        emit(failure(problem, kind="adapter_unavailable"))
        return
    try:
        emit(analyze(cindex, data, language))
    except RecursionError:
        emit(failure("source nesting is too deep to analyze"))
    except Exception as error:
        emit(failure("analysis failed: " + str(error)))


main()
