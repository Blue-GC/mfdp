#![allow(unused_imports)]

use proc_macro2::Span;
use serde_json::{json, Value};
use std::collections::{HashMap, HashSet};
use std::io::Read;
use syn::spanned::Spanned;
use syn::visit::Visit;
use syn::{
    AttrStyle, Attribute, Block, ConstParam, Expr, ExprCall, ExprField, ExprLit, ExprMethodCall, ExprPath,
    FnArg, Fields, ImplItem, Item, ItemStruct, Lit, Member, Meta, Pat, PatIdent, Path, ReturnType, Signature,
    TraitItem, Type, TypeParam, TypeParamBound, TypePath, UseTree, Visibility,
};

const MAX_SIGNATURE: usize = 240;

const UNIVERSE: &[&str] = &[
    "Vec", "Option", "Some", "None", "Result", "Ok", "Err", "String", "Box", "Default", "Clone", "Copy",
    "Send", "Sync", "Sized", "Drop", "Fn", "FnMut", "FnOnce", "Iterator", "IntoIterator", "From", "Into",
    "TryFrom", "TryInto", "ToString", "ToOwned", "AsRef", "AsMut", "Eq", "PartialEq", "Ord", "PartialOrd",
    "usize", "isize", "i8", "i16", "i32", "i64", "i128", "u8", "u16", "u32", "u64", "u128", "f32", "f64",
    "bool", "char", "str", "std", "core", "alloc", "crate", "super", "drop", "dyn", "impl",
];

fn collapse(s: &str) -> String {
    s.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn truncate(s: &str, limit: usize) -> String {
    let c = collapse(s);
    if c.chars().count() <= limit {
        c
    } else {
        let head: String = c.chars().take(limit - 3).collect();
        format!("{}...", head)
    }
}

struct Src {
    text: String,
    starts: Vec<usize>,
}

impl Src {
    fn new(text: String) -> Src {
        let mut starts = vec![0usize];
        for (i, b) in text.bytes().enumerate() {
            if b == b'\n' {
                starts.push(i + 1);
            }
        }
        Src { text, starts }
    }

    fn offset(&self, line: usize, col: usize) -> usize {
        if line == 0 || line > self.starts.len() {
            return self.text.len();
        }
        let ls = self.starts[line - 1];
        match self.text[ls..].char_indices().nth(col) {
            Some((i, _)) => ls + i,
            None => self.text.len(),
        }
    }

    fn start(&self, s: Span) -> usize {
        let lc = s.start();
        self.offset(lc.line, lc.column)
    }

    fn end(&self, s: Span) -> usize {
        let lc = s.end();
        self.offset(lc.line, lc.column)
    }

    fn slice(&self, a: usize, b: usize) -> String {
        if a <= b && b <= self.text.len() && self.text.is_char_boundary(a) && self.text.is_char_boundary(b) {
            self.text[a..b].to_string()
        } else {
            String::new()
        }
    }

    fn of<T: Spanned>(&self, n: &T) -> String {
        let sp = n.span();
        self.slice(self.start(sp), self.end(sp))
    }

    fn line_of(&self, off: usize) -> usize {
        match self.starts.binary_search(&off) {
            Ok(i) => i + 1,
            Err(i) => i,
        }
    }

    fn range<T: Spanned>(&self, node: &T, attrs: &[Attribute]) -> (usize, usize) {
        let sp = node.span();
        let mut start = self.start(sp);
        let end = self.end(sp);
        if let Some(last) = attrs.iter().filter(|a| matches!(a.style, AttrStyle::Outer)).last() {
            let attr_end = self.end(last.span());
            if attr_end > start && attr_end <= end {
                start = attr_end;
            }
        }
        let bytes = self.text.as_bytes();
        while start < end && bytes[start].is_ascii_whitespace() {
            start += 1;
        }
        (start, end)
    }

    fn header(&self, start: usize, end: usize) -> String {
        let bytes = self.text.as_bytes();
        let limit = end.min(bytes.len());
        let mut depth: i32 = 0;
        let mut i = start;
        while i < limit {
            let c = bytes[i];
            if c == b'"' {
                i += 1;
                while i < limit && bytes[i] != b'"' {
                    if bytes[i] == b'\\' {
                        i += 1;
                    }
                    i += 1;
                }
            } else if c == b'/' && i + 1 < limit && bytes[i + 1] == b'/' {
                while i < limit && bytes[i] != b'\n' {
                    i += 1;
                }
            } else if c == b'/' && i + 1 < limit && bytes[i + 1] == b'*' {
                i += 2;
                while i + 1 < limit && !(bytes[i] == b'*' && bytes[i + 1] == b'/') {
                    i += 1;
                }
                i += 1;
            } else if c == b'(' || c == b'[' {
                depth += 1;
            } else if c == b')' || c == b']' {
                depth -= 1;
            } else if depth == 0 && (c == b'{' || c == b';') {
                return collapse(&self.slice(start, i));
            }
            i += 1;
        }
        collapse(&self.slice(start, limit))
    }
}

fn path_name(p: &Path) -> Option<String> {
    let segs: Vec<String> = p.segments.iter().map(|s| s.ident.to_string()).collect();
    if segs.is_empty() {
        None
    } else {
        Some(segs.join("."))
    }
}

fn expr_dotted(e: &Expr) -> Option<String> {
    match e {
        Expr::Path(p) => path_name(&p.path),
        Expr::Field(f) => {
            let base = expr_dotted(&f.base)?;
            match &f.member {
                Member::Named(id) => Some(format!("{}.{}", base, id)),
                Member::Unnamed(_) => Some(base),
            }
        }
        Expr::Paren(p) => expr_dotted(&p.expr),
        Expr::Reference(r) => expr_dotted(&r.expr),
        Expr::Unary(u) => expr_dotted(&u.expr),
        _ => None,
    }
}

fn type_ident(t: &Type) -> Option<String> {
    match t {
        Type::Path(p) => p.path.segments.last().map(|s| s.ident.to_string()),
        Type::Reference(r) => type_ident(&r.elem),
        _ => None,
    }
}

#[derive(Default)]
struct Uses {
    calls: Vec<String>,
    refs: Vec<String>,
    locals: HashSet<String>,
}

impl<'ast> Visit<'ast> for Uses {
    fn visit_pat_ident(&mut self, i: &'ast PatIdent) {
        self.locals.insert(i.ident.to_string());
        syn::visit::visit_pat_ident(self, i);
    }

    fn visit_type_param(&mut self, i: &'ast TypeParam) {
        self.locals.insert(i.ident.to_string());
        syn::visit::visit_type_param(self, i);
    }

    fn visit_const_param(&mut self, i: &'ast ConstParam) {
        self.locals.insert(i.ident.to_string());
        syn::visit::visit_const_param(self, i);
    }

    fn visit_expr_call(&mut self, i: &'ast ExprCall) {
        if let Expr::Path(p) = &*i.func {
            if let Some(n) = path_name(&p.path) {
                self.calls.push(n);
            }
        }
        syn::visit::visit_expr_call(self, i);
    }

    fn visit_expr_method_call(&mut self, i: &'ast ExprMethodCall) {
        if let Some(base) = expr_dotted(&i.receiver) {
            self.calls.push(format!("{}.{}", base, i.method));
        }
        syn::visit::visit_expr_method_call(self, i);
    }

    fn visit_expr_path(&mut self, i: &'ast ExprPath) {
        if let Some(n) = path_name(&i.path) {
            self.refs.push(n);
        }
        syn::visit::visit_expr_path(self, i);
    }

    fn visit_type_path(&mut self, i: &'ast TypePath) {
        if let Some(n) = path_name(&i.path) {
            self.refs.push(n);
        }
        syn::visit::visit_type_path(self, i);
    }

    fn visit_expr_field(&mut self, i: &'ast ExprField) {
        if let (Some(base), Member::Named(id)) = (expr_dotted(&i.base), &i.member) {
            self.refs.push(format!("{}.{}", base, id));
        }
        syn::visit::visit_expr_field(self, i);
    }
}

fn finish(names: &[String], locals: &HashSet<String>) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    for n in names {
        let (root, rest) = match n.find('.') {
            Some(i) => (&n[..i], &n[i..]),
            None => (n.as_str(), ""),
        };
        let result = if root == "self" || root == "Self" {
            if rest.is_empty() {
                continue;
            }
            format!("this{}", rest)
        } else if locals.contains(root) || UNIVERSE.contains(&root) {
            continue;
        } else {
            n.clone()
        };
        if !out.contains(&result) {
            out.push(result);
        }
    }
    out
}

fn finish_uses(u: &Uses, extra: &HashSet<String>) -> (Vec<String>, Vec<String>) {
    let mut locals = u.locals.clone();
    for e in extra {
        locals.insert(e.clone());
    }
    (finish(&u.calls, &locals), finish(&u.refs, &locals))
}

fn doc_of(attrs: &[Attribute]) -> String {
    let mut lines: Vec<String> = Vec::new();
    for a in attrs {
        if a.path().is_ident("doc") {
            if let Meta::NameValue(nv) = &a.meta {
                if let Expr::Lit(ExprLit { lit: Lit::Str(s), .. }) = &nv.value {
                    lines.push(s.value().trim().to_string());
                }
            }
        }
    }
    lines.join("\n").trim().to_string()
}

fn exported_of(v: &Visibility) -> &'static str {
    match v {
        Visibility::Public(_) => "yes",
        Visibility::Inherited => "no",
        _ => "unknown",
    }
}

#[derive(Default)]
struct Spec {
    kind: String,
    parent: i64,
    name: String,
    qualified: String,
    signature: String,
    params: Vec<Value>,
    returns: String,
    decorators: Vec<String>,
    bases: Vec<String>,
    doc: String,
    start: usize,
    end: usize,
    exported: String,
    calls: Vec<String>,
    refs: Vec<String>,
}

struct Analyzer {
    src: Src,
    symbols: Vec<Value>,
    imports: Vec<Value>,
    exports: Vec<String>,
    type_index: HashMap<String, usize>,
    mod_index: HashMap<String, usize>,
}

impl Analyzer {
    fn new(src: Src) -> Analyzer {
        Analyzer {
            src,
            symbols: Vec::new(),
            imports: Vec::new(),
            exports: Vec::new(),
            type_index: HashMap::new(),
            mod_index: HashMap::new(),
        }
    }

    fn push(&mut self, s: Spec) -> usize {
        let source = self.src.slice(s.start, s.end);
        let start_line = self.src.line_of(s.start);
        let end_line = self.src.line_of(s.end.saturating_sub(1).max(s.start));
        self.symbols.push(json!({
            "kind": s.kind,
            "parent": s.parent,
            "name": s.name,
            "qualified": s.qualified,
            "signature": s.signature,
            "params": s.params,
            "returns": s.returns,
            "decorators": s.decorators,
            "bases": s.bases,
            "doc": s.doc,
            "startLine": start_line,
            "endLine": end_line,
            "exported": s.exported,
            "calls": s.calls,
            "refs": s.refs,
            "source": source,
            "defaultExport": false,
        }));
        self.symbols.len() - 1
    }

    fn decorators(&self, attrs: &[Attribute]) -> Vec<String> {
        attrs
            .iter()
            .filter(|a| matches!(a.style, AttrStyle::Outer) && !a.path().is_ident("doc"))
            .map(|a| collapse(&self.src.of(a)))
            .collect()
    }

    fn collect_types(&mut self, items: &[Item], parent: i64, prefix: &str) {
        for item in items {
            match item {
                Item::Mod(m) => {
                    if let Some((_, inner)) = &m.content {
                        let (start, end) = self.src.range(m, &m.attrs);
                        let name = m.ident.to_string();
                        let qualified = format!("{}{}", prefix, name);
                        let idx = self.push(Spec {
                            kind: "namespace".into(),
                            parent,
                            name: name.clone(),
                            qualified: qualified.clone(),
                            signature: self.src.header(start, end),
                            doc: doc_of(&m.attrs),
                            start,
                            end,
                            exported: exported_of(&m.vis).into(),
                            ..Default::default()
                        });
                        self.mod_index.insert(qualified.clone(), idx);
                        self.collect_types(inner, idx as i64, &format!("{}::", qualified));
                    }
                }
                Item::Struct(s) => {
                    let (start, end) = self.src.range(s, &s.attrs);
                    let mut v = Uses::default();
                    v.visit_item_struct(s);
                    let (calls, refs) = finish_uses(&v, &HashSet::new());
                    let name = s.ident.to_string();
                    let qualified = format!("{}{}", prefix, name);
                    let idx = self.push(Spec {
                        kind: "class".into(),
                        parent,
                        name: name.clone(),
                        qualified: qualified.clone(),
                        signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
                        decorators: self.decorators(&s.attrs),
                        doc: doc_of(&s.attrs),
                        start,
                        end,
                        exported: exported_of(&s.vis).into(),
                        calls,
                        refs,
                        ..Default::default()
                    });
                    self.type_index.insert(qualified.clone(), idx);
                    if matches!(s.vis, Visibility::Public(_)) && parent < 0 {
                        self.exports.push(name);
                    }
                    if let Fields::Named(named) = &s.fields {
                        for f in &named.named {
                            if let Some(id) = &f.ident {
                                let (fs, fe) = self.src.range(f, &f.attrs);
                                let mut fv = Uses::default();
                                fv.visit_type(&f.ty);
                                let (fc, fr) = finish_uses(&fv, &HashSet::new());
                                self.push(Spec {
                                    kind: "property".into(),
                                    parent: idx as i64,
                                    name: id.to_string(),
                                    qualified: format!("{}::{}", qualified, id),
                                    signature: truncate(&self.src.slice(fs, fe), MAX_SIGNATURE),
                                    returns: collapse(&self.src.of(&f.ty)),
                                    doc: doc_of(&f.attrs),
                                    start: fs,
                                    end: fe,
                                    exported: "unknown".into(),
                                    calls: fc,
                                    refs: fr,
                                    ..Default::default()
                                });
                            }
                        }
                    }
                }
                Item::Union(u) => {
                    let (start, end) = self.src.range(u, &u.attrs);
                    let name = u.ident.to_string();
                    let qualified = format!("{}{}", prefix, name);
                    let idx = self.push(Spec {
                        kind: "class".into(),
                        parent,
                        name: name.clone(),
                        qualified: qualified.clone(),
                        signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
                        doc: doc_of(&u.attrs),
                        start,
                        end,
                        exported: exported_of(&u.vis).into(),
                        ..Default::default()
                    });
                    self.type_index.insert(qualified, idx);
                }
                Item::Enum(e) => {
                    let (start, end) = self.src.range(e, &e.attrs);
                    let mut v = Uses::default();
                    v.visit_item_enum(e);
                    let (calls, refs) = finish_uses(&v, &HashSet::new());
                    let name = e.ident.to_string();
                    let qualified = format!("{}{}", prefix, name);
                    let variants: Vec<String> = e.variants.iter().map(|x| x.ident.to_string()).collect();
                    let sig = format!("{} {{ {} }}", self.src.header(start, end), variants.join(", "));
                    let idx = self.push(Spec {
                        kind: "enum".into(),
                        parent,
                        name: name.clone(),
                        qualified: qualified.clone(),
                        signature: truncate(&sig, MAX_SIGNATURE),
                        decorators: self.decorators(&e.attrs),
                        doc: doc_of(&e.attrs),
                        start,
                        end,
                        exported: exported_of(&e.vis).into(),
                        calls,
                        refs,
                        ..Default::default()
                    });
                    self.type_index.insert(qualified, idx);
                    if matches!(e.vis, Visibility::Public(_)) && parent < 0 {
                        self.exports.push(name);
                    }
                }
                Item::Trait(t) => {
                    let (start, end) = self.src.range(t, &t.attrs);
                    let mut bases: Vec<String> = Vec::new();
                    for b in &t.supertraits {
                        if let TypeParamBound::Trait(tb) = b {
                            if let Some(n) = path_name(&tb.path) {
                                bases.push(n);
                            }
                        }
                    }
                    let name = t.ident.to_string();
                    let qualified = format!("{}{}", prefix, name);
                    let idx = self.push(Spec {
                        kind: "interface".into(),
                        parent,
                        name: name.clone(),
                        qualified: qualified.clone(),
                        signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
                        bases,
                        decorators: self.decorators(&t.attrs),
                        doc: doc_of(&t.attrs),
                        start,
                        end,
                        exported: exported_of(&t.vis).into(),
                        ..Default::default()
                    });
                    self.type_index.insert(qualified.clone(), idx);
                    if matches!(t.vis, Visibility::Public(_)) && parent < 0 {
                        self.exports.push(name);
                    }
                    for ti in &t.items {
                        if let TraitItem::Fn(m) = ti {
                            let (ms, me) = self.src.range(m, &m.attrs);
                            let extra = HashSet::new();
                            self.add_fn(
                                ms,
                                me,
                                &m.attrs,
                                "unknown",
                                &m.sig,
                                m.default.as_ref(),
                                idx as i64,
                                &format!("{}::{}", qualified, m.sig.ident),
                                &extra,
                            );
                        }
                    }
                }
                Item::Type(t) => {
                    let (start, end) = self.src.range(t, &t.attrs);
                    let mut v = Uses::default();
                    v.visit_item_type(t);
                    let (calls, refs) = finish_uses(&v, &HashSet::new());
                    let name = t.ident.to_string();
                    let qualified = format!("{}{}", prefix, name);
                    let idx = self.push(Spec {
                        kind: "type".into(),
                        parent,
                        name: name.clone(),
                        qualified: qualified.clone(),
                        signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
                        doc: doc_of(&t.attrs),
                        start,
                        end,
                        exported: exported_of(&t.vis).into(),
                        calls,
                        refs,
                        ..Default::default()
                    });
                    self.type_index.insert(qualified, idx);
                    if matches!(t.vis, Visibility::Public(_)) && parent < 0 {
                        self.exports.push(name);
                    }
                }
                _ => {}
            }
        }
    }

    fn add_fn(
        &mut self,
        start: usize,
        end: usize,
        attrs: &[Attribute],
        exported: &str,
        sig: &Signature,
        body: Option<&Block>,
        parent: i64,
        qualified: &str,
        extra: &HashSet<String>,
    ) {
        let mut v = Uses::default();
        v.visit_signature(sig);
        if let Some(b) = body {
            v.visit_block(b);
        }
        let (calls, refs) = finish_uses(&v, extra);
        let mut params: Vec<Value> = Vec::new();
        for arg in &sig.inputs {
            match arg {
                FnArg::Receiver(r) => params.push(json!({
                    "name": "self",
                    "annotation": collapse(&self.src.of(r)),
                    "default": "",
                })),
                FnArg::Typed(pt) => params.push(json!({
                    "name": collapse(&self.src.of(&*pt.pat)),
                    "annotation": collapse(&self.src.of(&*pt.ty)),
                    "default": "",
                })),
            }
        }
        let returns = match &sig.output {
            ReturnType::Default => String::new(),
            ReturnType::Type(_, ty) => collapse(&self.src.of(&**ty)),
        };
        let name = sig.ident.to_string();
        let kind = if parent >= 0 { "method" } else { "function" };
        self.push(Spec {
            kind: kind.into(),
            parent,
            name,
            qualified: qualified.to_string(),
            signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
            params,
            returns,
            decorators: self.decorators(attrs),
            doc: doc_of(attrs),
            start,
            end,
            exported: exported.into(),
            calls,
            refs,
            ..Default::default()
        });
    }

    fn collect_rest(&mut self, items: &[Item], parent: i64, prefix: &str) {
        for item in items {
            match item {
                Item::Mod(m) => {
                    if let Some((_, inner)) = &m.content {
                        let qualified = format!("{}{}", prefix, m.ident);
                        let idx = self.mod_index.get(&qualified).copied().map(|i| i as i64).unwrap_or(parent);
                        self.collect_rest(inner, idx, &format!("{}::", qualified));
                    }
                }
                Item::Fn(f) => {
                    let (start, end) = self.src.range(f, &f.attrs);
                    let extra = HashSet::new();
                    if matches!(f.vis, Visibility::Public(_)) && parent < 0 {
                        self.exports.push(f.sig.ident.to_string());
                    }
                    self.add_fn(
                        start,
                        end,
                        &f.attrs,
                        exported_of(&f.vis),
                        &f.sig,
                        Some(&*f.block),
                        parent,
                        &format!("{}{}", prefix, f.sig.ident),
                        &extra,
                    );
                }
                Item::Const(c) => {
                    let (start, end) = self.src.range(c, &c.attrs);
                    let mut v = Uses::default();
                    v.visit_item_const(c);
                    let (calls, refs) = finish_uses(&v, &HashSet::new());
                    let name = c.ident.to_string();
                    if matches!(c.vis, Visibility::Public(_)) && parent < 0 {
                        self.exports.push(name.clone());
                    }
                    self.push(Spec {
                        kind: "constant".into(),
                        parent,
                        name: name.clone(),
                        qualified: format!("{}{}", prefix, name),
                        signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
                        returns: collapse(&self.src.of(&*c.ty)),
                        doc: doc_of(&c.attrs),
                        start,
                        end,
                        exported: exported_of(&c.vis).into(),
                        calls,
                        refs,
                        ..Default::default()
                    });
                }
                Item::Static(s) => {
                    let (start, end) = self.src.range(s, &s.attrs);
                    let mut v = Uses::default();
                    v.visit_item_static(s);
                    let (calls, refs) = finish_uses(&v, &HashSet::new());
                    let name = s.ident.to_string();
                    if matches!(s.vis, Visibility::Public(_)) && parent < 0 {
                        self.exports.push(name.clone());
                    }
                    self.push(Spec {
                        kind: "variable".into(),
                        parent,
                        name: name.clone(),
                        qualified: format!("{}{}", prefix, name),
                        signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
                        returns: collapse(&self.src.of(&*s.ty)),
                        doc: doc_of(&s.attrs),
                        start,
                        end,
                        exported: exported_of(&s.vis).into(),
                        calls,
                        refs,
                        ..Default::default()
                    });
                }
                Item::Macro(m) => {
                    if let Some(id) = &m.ident {
                        let (start, end) = self.src.range(m, &m.attrs);
                        self.push(Spec {
                            kind: "function".into(),
                            parent,
                            name: id.to_string(),
                            qualified: format!("{}{}", prefix, id),
                            signature: format!("macro_rules! {}", id),
                            doc: doc_of(&m.attrs),
                            start,
                            end,
                            exported: "unknown".into(),
                            ..Default::default()
                        });
                    }
                }
                Item::Use(u) => self.add_use(u),
                Item::Impl(imp) => self.add_impl(imp, prefix),
                _ => {}
            }
        }
    }

    fn add_impl(&mut self, imp: &syn::ItemImpl, prefix: &str) {
        let type_name = type_ident(&imp.self_ty);
        let (parent, qual_prefix, key) = match &type_name {
            Some(n) => {
                let key = format!("{}{}", prefix, n);
                match self.type_index.get(&key) {
                    Some(i) => (*i as i64, key.clone(), Some(key)),
                    None => (-1, key.clone(), None),
                }
            }
            None => (-1, format!("{}impl", prefix), None),
        };
        if let (Some((_, tr, _)), Some(k)) = (&imp.trait_, &key) {
            if let (Some(trait_name), Some(idx)) = (path_name(tr), self.type_index.get(k).copied()) {
                if let Some(arr) = self.symbols[idx]["bases"].as_array_mut() {
                    if !arr.iter().any(|b| b.as_str() == Some(trait_name.as_str())) {
                        arr.push(json!(trait_name));
                    }
                }
            }
        }
        let mut extra: HashSet<String> = HashSet::new();
        for tp in imp.generics.type_params() {
            extra.insert(tp.ident.to_string());
        }
        for it in &imp.items {
            match it {
                ImplItem::Fn(m) => {
                    let (start, end) = self.src.range(m, &m.attrs);
                    self.add_fn(
                        start,
                        end,
                        &m.attrs,
                        exported_of(&m.vis),
                        &m.sig,
                        Some(&m.block),
                        parent,
                        &format!("{}::{}", qual_prefix, m.sig.ident),
                        &extra,
                    );
                }
                ImplItem::Const(c) => {
                    let (start, end) = self.src.range(c, &c.attrs);
                    self.push(Spec {
                        kind: "property".into(),
                        parent,
                        name: c.ident.to_string(),
                        qualified: format!("{}::{}", qual_prefix, c.ident),
                        signature: truncate(&self.src.header(start, end), MAX_SIGNATURE),
                        returns: collapse(&self.src.of(&c.ty)),
                        doc: doc_of(&c.attrs),
                        start,
                        end,
                        exported: exported_of(&c.vis).into(),
                        ..Default::default()
                    });
                }
                _ => {}
            }
        }
    }

    fn flatten_use(
        tree: &UseTree,
        prefix: &mut Vec<String>,
        bindings: &mut Vec<(String, String, String)>,
        globs: &mut Vec<String>,
    ) {
        match tree {
            UseTree::Path(p) => {
                prefix.push(p.ident.to_string());
                Self::flatten_use(&p.tree, prefix, bindings, globs);
                prefix.pop();
            }
            UseTree::Name(n) => {
                let ident = n.ident.to_string();
                if ident == "self" {
                    if let Some(last) = prefix.last() {
                        let module = prefix[..prefix.len() - 1].join("::");
                        bindings.push((module, last.clone(), last.clone()));
                    }
                } else {
                    bindings.push((prefix.join("::"), ident.clone(), ident));
                }
            }
            UseTree::Rename(r) => {
                let ident = r.ident.to_string();
                let local = r.rename.to_string();
                if ident == "self" {
                    if let Some(last) = prefix.last() {
                        let module = prefix[..prefix.len() - 1].join("::");
                        bindings.push((module, last.clone(), local));
                    }
                } else {
                    bindings.push((prefix.join("::"), ident, local));
                }
            }
            UseTree::Glob(_) => globs.push(prefix.join("::")),
            UseTree::Group(g) => {
                for item in &g.items {
                    Self::flatten_use(item, prefix, bindings, globs);
                }
            }
        }
    }

    fn add_use(&mut self, u: &syn::ItemUse) {
        let (start, end) = self.src.range(u, &u.attrs);
        let text = collapse(&self.src.slice(start, end));
        let line = self.src.line_of(start);
        let end_line = self.src.line_of(end.saturating_sub(1).max(start));
        let mut bindings: Vec<(String, String, String)> = Vec::new();
        let mut globs: Vec<String> = Vec::new();
        let mut prefix: Vec<String> = Vec::new();
        Self::flatten_use(&u.tree, &mut prefix, &mut bindings, &mut globs);
        if !bindings.is_empty() {
            let module = bindings[0].0.clone();
            let list: Vec<Value> = bindings
                .iter()
                .map(|(m, imported, local)| json!({"local": local, "imported": imported, "module": m, "depth": 1}))
                .collect();
            self.imports.push(json!({
                "line": line,
                "endLine": end_line,
                "text": text,
                "module": module,
                "star": false,
                "bindings": list,
            }));
        }
        for g in globs {
            self.imports.push(json!({
                "line": line,
                "endLine": end_line,
                "text": text,
                "module": g,
                "star": true,
                "bindings": [],
            }));
        }
    }
}

fn failure(message: &str, line: usize, column: usize, kind: &str) -> Value {
    json!({"ok": false, "error": {"kind": kind, "message": message, "line": line, "column": column}})
}

fn main() {
    let mut data = Vec::new();
    if std::io::stdin().read_to_end(&mut data).is_err() {
        println!("{}", failure("cannot read source", 0, 0, "parse_error"));
        return;
    }
    let text = match String::from_utf8(data) {
        Ok(t) => t,
        Err(_) => {
            println!("{}", failure("source is not valid UTF-8", 0, 0, "parse_error"));
            return;
        }
    };
    let text = text.strip_prefix('\u{feff}').map(|s| s.to_string()).unwrap_or(text);
    let file = match syn::parse_file(&text) {
        Ok(f) => f,
        Err(e) => {
            let lc = e.span().start();
            println!("{}", failure(&e.to_string(), lc.line, lc.column + 1, "parse_error"));
            return;
        }
    };
    let mut a = Analyzer::new(Src::new(text));
    a.collect_types(&file.items, -1, "");
    a.collect_rest(&file.items, -1, "");
    let out = json!({
        "ok": true,
        "language": "rust",
        "imports": a.imports,
        "symbols": a.symbols,
        "exports": a.exports,
    });
    println!("{}", out);
}
