import com.sun.source.tree.*;
import com.sun.source.util.*;
import java.io.*;
import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.*;
import java.util.*;
import javax.lang.model.element.Modifier;
import javax.tools.*;

public class JavaAdapter {
    static String src;
    static CompilationUnitTree unit;
    static SourcePositions positions;
    static DocTrees docTrees;
    static final List<Map<String, Object>> symbols = new ArrayList<>();
    static final List<Map<String, Object>> imports = new ArrayList<>();
    static final List<String> exportNames = new ArrayList<>();
    static final Set<String> importLocals = new HashSet<>();
    static final Set<String> universe = new HashSet<>(Arrays.asList(
        "String", "Object", "Integer", "Long", "Double", "Float", "Boolean", "Byte", "Short", "Character",
        "Number", "Math", "System", "Exception", "RuntimeException", "Throwable", "Error", "Thread",
        "Runnable", "Iterable", "Comparable", "Override", "Deprecated", "SuppressWarnings",
        "FunctionalInterface", "SafeVarargs", "Class", "Enum", "Record", "Void", "StringBuilder",
        "StringBuffer", "CharSequence", "AutoCloseable", "Cloneable", "IllegalArgumentException",
        "IllegalStateException", "NullPointerException", "UnsupportedOperationException",
        "IndexOutOfBoundsException", "ArithmeticException", "ClassCastException", "InterruptedException",
        "StackTraceElement", "Process", "Runtime", "ThreadLocal", "var", "java", "javax", "length"));

    static final int MAX_SIGNATURE = 240;

    static class ClassInfo {
        String qualified;
        Set<String> members = new HashSet<>();
        Set<String> typeParams = new HashSet<>();
        Map<String, String> fieldTypes = new HashMap<>();
        ClassInfo outer;
    }

    static String collapse(String s) {
        return String.join(" ", s.trim().split("\\s+"));
    }

    static String truncate(String s, int limit) {
        String c = collapse(s);
        return c.length() <= limit ? c : c.substring(0, limit - 3) + "...";
    }

    static int startPos(Tree t) {
        return (int) positions.getStartPosition(unit, t);
    }

    static int endPos(Tree t) {
        return (int) positions.getEndPosition(unit, t);
    }

    static String slice(int a, int b) {
        if (a < 0 || b < a || b > src.length()) return "";
        return src.substring(a, b);
    }

    static String text(Tree t) {
        return t == null ? "" : slice(startPos(t), endPos(t));
    }

    static int line(int pos) {
        return (int) unit.getLineMap().getLineNumber(Math.max(pos, 0));
    }

    static int skipQuoted(int i, char quote) {
        if (quote == '"' && src.startsWith("\"\"\"", i)) {
            int end = src.indexOf("\"\"\"", i + 3);
            return end < 0 ? src.length() : end + 2;
        }
        int j = i + 1;
        while (j < src.length()) {
            char c = src.charAt(j);
            if (c == '\\') {
                j += 2;
                continue;
            }
            if (c == quote) return j;
            j++;
        }
        return src.length();
    }

    static String header(int start, int limit) {
        int depth = 0;
        for (int i = start; i < limit && i < src.length(); i++) {
            char c = src.charAt(i);
            if (c == '"' || c == '\'') {
                i = skipQuoted(i, c);
                continue;
            }
            if (c == '/' && i + 1 < src.length()) {
                char n = src.charAt(i + 1);
                if (n == '/') {
                    int nl = src.indexOf('\n', i);
                    i = nl < 0 ? src.length() : nl;
                    continue;
                }
                if (n == '*') {
                    int end = src.indexOf("*/", i + 2);
                    i = end < 0 ? src.length() : end + 1;
                    continue;
                }
            }
            if (c == '(') depth++;
            else if (c == ')') depth--;
            else if (depth == 0 && (c == '{' || c == ';')) return collapse(src.substring(start, i));
        }
        return collapse(slice(start, Math.min(limit, src.length())));
    }

    static String dotted(Tree e) {
        if (e instanceof IdentifierTree) return ((IdentifierTree) e).getName().toString();
        if (e instanceof MemberSelectTree) {
            MemberSelectTree m = (MemberSelectTree) e;
            String base = dotted(m.getExpression());
            return base == null ? null : base + "." + m.getIdentifier();
        }
        if (e instanceof ParameterizedTypeTree) return dotted(((ParameterizedTypeTree) e).getType());
        if (e instanceof ParenthesizedTree) return dotted(((ParenthesizedTree) e).getExpression());
        if (e instanceof AnnotatedTypeTree) return dotted(((AnnotatedTypeTree) e).getUnderlyingType());
        return null;
    }

    static class Uses {
        List<String> calls = new ArrayList<>();
        List<String> refs = new ArrayList<>();
    }

    static class LocalScanner extends TreeScanner<Void, Void> {
        final Set<String> locals = new HashSet<>();
        final Map<String, String> localTypes = new HashMap<>();
        final Tree root;

        LocalScanner(Tree root) {
            this.root = root;
        }

        @Override
        public Void visitVariable(VariableTree node, Void p) {
            String name = node.getName().toString();
            locals.add(name);
            String type = typeName(node.getType());
            if (type != null) localTypes.put(name, type);
            return super.visitVariable(node, p);
        }

        @Override
        public Void visitTypeParameter(TypeParameterTree node, Void p) {
            locals.add(node.getName().toString());
            return super.visitTypeParameter(node, p);
        }

        @Override
        public Void visitClass(ClassTree node, Void p) {
            if (node != root && !node.getSimpleName().isEmpty()) locals.add(node.getSimpleName().toString());
            return super.visitClass(node, p);
        }
    }

    static class UseScanner extends TreeScanner<Void, Void> {
        final List<String> calls = new ArrayList<>();
        final List<String> loads = new ArrayList<>();

        @Override
        public Void visitMethodInvocation(MethodInvocationTree node, Void p) {
            String d = dotted(node.getMethodSelect());
            if (d != null) calls.add(d);
            return super.visitMethodInvocation(node, p);
        }

        @Override
        public Void visitNewClass(NewClassTree node, Void p) {
            String d = dotted(node.getIdentifier());
            if (d != null) calls.add(d);
            return super.visitNewClass(node, p);
        }

        @Override
        public Void visitMemberSelect(MemberSelectTree node, Void p) {
            String d = dotted(node);
            if (d != null && d.startsWith("this.")) loads.add(d);
            return super.visitMemberSelect(node, p);
        }

        @Override
        public Void visitIdentifier(IdentifierTree node, Void p) {
            String name = node.getName().toString();
            if (!name.equals("this") && !name.equals("super")) loads.add(name);
            return null;
        }
    }

    static boolean isTypeParam(ClassInfo info, String name) {
        for (ClassInfo c = info; c != null; c = c.outer) {
            if (c.typeParams.contains(name)) return true;
        }
        return false;
    }

    static List<String> normalize(List<String> names, Set<String> locals, Map<String, String> localTypes,
                                  ClassInfo info, boolean isCall) {
        List<String> out = new ArrayList<>();
        for (String n : names) {
            int dot = n.indexOf('.');
            String root = dot < 0 ? n : n.substring(0, dot);
            String result = n;
            if (root.equals("this")) {
                if (n.equals("this")) continue;
                String[] parts = n.split("\\.", 3);
                if (parts.length == 3 && info != null && info.fieldTypes.containsKey(parts[1])) {
                    String t = info.fieldTypes.get(parts[1]);
                    if (universe.contains(rootOf(t)) || isTypeParam(info, rootOf(t))) continue;
                    result = t + "." + parts[2];
                }
            } else if (root.equals("super")) {
                continue;
            } else if (locals.contains(root)) {
                String t = localTypes.get(root);
                if (t != null && dot >= 0 && !universe.contains(rootOf(t)) && !isTypeParam(info, rootOf(t))) {
                    String typed = t + n.substring(dot);
                    if (!out.contains(typed)) out.add(typed);
                }
                continue;
            } else if (isTypeParam(info, root)) {
                continue;
            } else if (universe.contains(root) && !(info != null && info.members.contains(root))) {
                continue;
            } else {
                ClassInfo owner = null;
                for (ClassInfo c = info; c != null; c = c.outer) {
                    if (c.members.contains(root)) {
                        owner = c;
                        break;
                    }
                }
                String fieldType = owner == null ? null : owner.fieldTypes.get(root);
                if (fieldType != null && dot >= 0) {
                    if (universe.contains(rootOf(fieldType)) || isTypeParam(owner, rootOf(fieldType))) continue;
                    result = fieldType + n.substring(dot);
                } else if (owner == info && owner != null) {
                    result = "this." + n;
                } else if (owner != null) {
                    result = owner.qualified + "." + n;
                } else if (isCall && dot < 0 && !importLocals.contains(root) && info != null) {
                    result = "this." + n;
                }
            }
            if (!out.contains(result)) out.add(result);
        }
        return out;
    }

    static Uses uses(List<? extends Tree> roots, Tree localRoot, ClassInfo info) {
        LocalScanner ls = new LocalScanner(localRoot);
        UseScanner us = new UseScanner();
        for (Tree r : roots) {
            if (r != null) {
                ls.scan(r, null);
                us.scan(r, null);
            }
        }
        Uses u = new Uses();
        u.calls = normalize(us.calls, ls.locals, ls.localTypes, info, true);
        u.refs = normalize(us.loads, ls.locals, ls.localTypes, info, false);
        return u;
    }

    static ClassInfo infoFor(ClassTree ct, ClassInfo outer, String qualified) {
        ClassInfo ci = new ClassInfo();
        ci.qualified = qualified;
        ci.outer = outer;
        for (TypeParameterTree tp : ct.getTypeParameters()) ci.typeParams.add(tp.getName().toString());
        for (Tree m : ct.getMembers()) {
            if (m instanceof VariableTree) {
                VariableTree vt = (VariableTree) m;
                ci.members.add(vt.getName().toString());
                String ft = typeName(vt.getType());
                if (ft != null) ci.fieldTypes.put(vt.getName().toString(), ft);
            }
            else if (m instanceof MethodTree) {
                String n = ((MethodTree) m).getName().toString();
                if (!n.equals("<init>")) ci.members.add(n);
            } else if (m instanceof ClassTree) ci.members.add(((ClassTree) m).getSimpleName().toString());
        }
        return ci;
    }

    static String exportedFlag(ModifiersTree mods, boolean topLevel) {
        Set<Modifier> flags = mods.getFlags();
        if (flags.contains(Modifier.PUBLIC)) return "yes";
        if (flags.contains(Modifier.PRIVATE)) return "no";
        return topLevel ? "no" : "unknown";
    }

    static List<String> annotationsOf(ModifiersTree mods) {
        List<String> out = new ArrayList<>();
        for (AnnotationTree a : mods.getAnnotations()) out.add(collapse(text(a)));
        return out;
    }

    static String typeName(Tree t) {
        if (t == null || t instanceof PrimitiveTypeTree || t instanceof ArrayTypeTree) return null;
        String d = dotted(t);
        if (d == null || d.equals("var")) return null;
        return d;
    }

    static String rootOf(String name) {
        int dot = name.indexOf('.');
        return dot < 0 ? name : name.substring(0, dot);
    }

    static String stripGenerics(String s) {
        int i = s.indexOf('<');
        return (i >= 0 ? s.substring(0, i) : s).trim();
    }

    static String docOf(TreePath path) {
        try {
            String d = docTrees.getDocComment(path);
            return d == null ? "" : d.trim();
        } catch (Exception e) {
            return "";
        }
    }

    static int addSymbol(String kind, int parent, String name, String qualified, String signature,
                         List<Map<String, Object>> params, String returns, List<String> decorators,
                         List<String> bases, String doc, int start, int end, String exported,
                         Uses uses, String source) {
        Map<String, Object> s = new LinkedHashMap<>();
        s.put("kind", kind);
        s.put("parent", parent);
        s.put("name", name);
        s.put("qualified", qualified);
        s.put("signature", signature);
        s.put("params", params);
        s.put("returns", returns);
        s.put("decorators", decorators);
        s.put("bases", bases);
        s.put("doc", doc);
        s.put("startLine", line(start));
        s.put("endLine", line(Math.max(start, end - 1)));
        s.put("exported", exported);
        s.put("calls", uses == null ? new ArrayList<String>() : uses.calls);
        s.put("refs", uses == null ? new ArrayList<String>() : uses.refs);
        s.put("source", source);
        s.put("defaultExport", false);
        symbols.add(s);
        return symbols.size() - 1;
    }

    static void visitClass(TreePath path, int parent, ClassInfo outer, String outerQualified) {
        ClassTree ct = (ClassTree) path.getLeaf();
        String name = ct.getSimpleName().toString();
        if (name.isEmpty()) return;
        int start = startPos(ct);
        int end = endPos(ct);
        if (start < 0) return;
        Tree.Kind k = ct.getKind();
        String kind = (k == Tree.Kind.INTERFACE || k == Tree.Kind.ANNOTATION_TYPE) ? "interface"
            : (k == Tree.Kind.ENUM ? "enum" : "class");
        String qualified = outerQualified == null ? name : outerQualified + "." + name;
        ClassInfo info = infoFor(ct, outer, qualified);
        String head = header(start, end);
        if (k == Tree.Kind.ENUM) {
            List<String> constants = new ArrayList<>();
            for (Tree m : ct.getMembers()) {
                if (m instanceof VariableTree) {
                    VariableTree vt = (VariableTree) m;
                    if (vt.getInitializer() instanceof NewClassTree && startPos(vt) >= 0
                        && vt.getType() instanceof IdentifierTree
                        && ((IdentifierTree) vt.getType()).getName().contentEquals(name)) {
                        constants.add(vt.getName().toString());
                    }
                }
            }
            head = truncate(head + " { " + String.join(", ", constants) + " }", MAX_SIGNATURE);
        } else {
            head = truncate(head, MAX_SIGNATURE);
        }
        List<String> bases = new ArrayList<>();
        if (ct.getExtendsClause() != null) bases.add(stripGenerics(collapse(text(ct.getExtendsClause()))));
        for (Tree t : ct.getImplementsClause()) bases.add(stripGenerics(collapse(text(t))));
        List<Tree> roots = new ArrayList<>();
        roots.add(ct.getModifiers());
        roots.addAll(ct.getTypeParameters());
        if (ct.getExtendsClause() != null) roots.add(ct.getExtendsClause());
        roots.addAll(ct.getImplementsClause());
        Uses u = uses(roots, ct, outer == null ? info : info);
        boolean topLevel = parent < 0;
        int idx = addSymbol(kind, parent, name, qualified, head, new ArrayList<>(), "",
            annotationsOf(ct.getModifiers()), bases, docOf(path), start, end,
            exportedFlag(ct.getModifiers(), topLevel), u, slice(start, end));
        if (topLevel && ct.getModifiers().getFlags().contains(Modifier.PUBLIC)) exportNames.add(name);
        for (Tree m : ct.getMembers()) {
            if (startPos(m) < 0) continue;
            TreePath mp = new TreePath(path, m);
            if (m instanceof MethodTree) visitMethod(mp, idx, info, qualified, name);
            else if (m instanceof VariableTree) visitField(mp, idx, info, qualified, name, k == Tree.Kind.ENUM);
            else if (m instanceof ClassTree) visitClass(mp, idx, info, qualified);
        }
    }

    static void visitMethod(TreePath path, int parent, ClassInfo info, String classQualified,
                            String className) {
        MethodTree mt = (MethodTree) path.getLeaf();
        boolean ctor = mt.getName().contentEquals("<init>");
        String name = ctor ? className : mt.getName().toString();
        int start = startPos(mt);
        int end = endPos(mt);
        List<Map<String, Object>> params = new ArrayList<>();
        for (VariableTree p : mt.getParameters()) {
            Map<String, Object> pm = new LinkedHashMap<>();
            String full = collapse(text(p));
            String type = collapse(text(p.getType()));
            if (full.contains("...") && !type.endsWith("...")) type += "...";
            pm.put("name", p.getName().toString());
            pm.put("annotation", type);
            pm.put("default", "");
            params.add(pm);
        }
        String returns = mt.getReturnType() == null ? "" : collapse(text(mt.getReturnType()));
        List<Tree> roots = new ArrayList<>();
        roots.add(mt);
        Uses u = uses(roots, mt, info);
        addSymbol(ctor ? "constructor" : "method", parent, name, classQualified + "." + name,
            truncate(header(start, end), MAX_SIGNATURE), params, returns, annotationsOf(mt.getModifiers()),
            new ArrayList<>(), docOf(path), start, end, exportedFlag(mt.getModifiers(), false), u,
            slice(start, end));
    }

    static void visitField(TreePath path, int parent, ClassInfo info, String classQualified,
                           String className, boolean inEnum) {
        VariableTree vt = (VariableTree) path.getLeaf();
        int start = startPos(vt);
        int end = endPos(vt);
        Set<Modifier> flags = vt.getModifiers().getFlags();
        boolean enumConstant = inEnum && vt.getInitializer() instanceof NewClassTree
            && vt.getType() instanceof IdentifierTree
            && ((IdentifierTree) vt.getType()).getName().contentEquals(className);
        boolean constant = enumConstant || (flags.contains(Modifier.STATIC) && flags.contains(Modifier.FINAL));
        List<Tree> roots = new ArrayList<>();
        roots.add(vt);
        Uses u = enumConstant ? null : uses(roots, vt, info);
        String name = vt.getName().toString();
        addSymbol(constant ? "constant" : "property", parent, name, classQualified + "." + name,
            truncate(slice(start, end), MAX_SIGNATURE), new ArrayList<>(), collapse(text(vt.getType())),
            annotationsOf(vt.getModifiers()), new ArrayList<>(), docOf(path), start, end,
            exportedFlag(vt.getModifiers(), false), u, slice(start, end));
    }

    static void collectImports() {
        for (ImportTree imp : unit.getImports()) {
            String q = imp.getQualifiedIdentifier().toString();
            int s = startPos(imp);
            int e = endPos(imp);
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("line", line(s));
            m.put("endLine", line(Math.max(s, e - 1)));
            m.put("text", collapse(slice(s, e)));
            List<Map<String, Object>> bindings = new ArrayList<>();
            boolean star = q.endsWith(".*");
            String module;
            if (star) {
                module = q.substring(0, q.length() - 2);
            } else {
                int dot = q.lastIndexOf('.');
                String simple = dot < 0 ? q : q.substring(dot + 1);
                module = imp.isStatic() && dot >= 0 ? q.substring(0, dot) : q;
                Map<String, Object> b = new LinkedHashMap<>();
                b.put("local", simple);
                b.put("imported", simple);
                b.put("module", module);
                b.put("depth", 1);
                bindings.add(b);
                importLocals.add(simple);
            }
            m.put("module", module);
            m.put("star", star);
            m.put("bindings", bindings);
            imports.add(m);
        }
    }

    static void json(StringBuilder sb, Object o) {
        if (o == null) {
            sb.append("null");
        } else if (o instanceof String) {
            String s = (String) o;
            sb.append('"');
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                switch (c) {
                    case '"': sb.append("\\\""); break;
                    case '\\': sb.append("\\\\"); break;
                    case '\n': sb.append("\\n"); break;
                    case '\r': sb.append("\\r"); break;
                    case '\t': sb.append("\\t"); break;
                    default:
                        if (c < 0x20 || c > 0x7e) sb.append(String.format("\\u%04x", (int) c));
                        else sb.append(c);
                }
            }
            sb.append('"');
        } else if (o instanceof Number || o instanceof Boolean) {
            sb.append(o.toString());
        } else if (o instanceof Map) {
            sb.append('{');
            boolean first = true;
            for (Map.Entry<?, ?> e : ((Map<?, ?>) o).entrySet()) {
                if (!first) sb.append(',');
                first = false;
                json(sb, e.getKey().toString());
                sb.append(':');
                json(sb, e.getValue());
            }
            sb.append('}');
        } else if (o instanceof Collection) {
            sb.append('[');
            boolean first = true;
            for (Object x : (Collection<?>) o) {
                if (!first) sb.append(',');
                first = false;
                json(sb, x);
            }
            sb.append(']');
        } else {
            json(sb, o.toString());
        }
    }

    static void emit(Map<String, Object> result) {
        StringBuilder sb = new StringBuilder();
        json(sb, result);
        System.out.println(sb);
        System.out.flush();
    }

    static void fail(String kind, String message, long line, long column) {
        Map<String, Object> err = new LinkedHashMap<>();
        err.put("kind", kind);
        err.put("message", message);
        err.put("line", line);
        err.put("column", column);
        Map<String, Object> result = new LinkedHashMap<>();
        result.put("ok", false);
        result.put("error", err);
        emit(result);
    }

    public static void main(String[] args) throws Exception {
        byte[] data = System.in.readAllBytes();
        try {
            src = StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(data)).toString();
        } catch (CharacterCodingException e) {
            fail("parse_error", "source is not valid UTF-8", 0, 0);
            return;
        }
        if (src.startsWith("\ufeff")) src = src.substring(1);
        JavaCompiler compiler = ToolProvider.getSystemJavaCompiler();
        if (compiler == null) {
            fail("adapter_unavailable", "a JDK is required (the runtime has no compiler module)", 0, 0);
            return;
        }
        DiagnosticCollector<JavaFileObject> diags = new DiagnosticCollector<>();
        JavaFileObject file = new SimpleJavaFileObject(URI.create("string:///Source.java"),
            JavaFileObject.Kind.SOURCE) {
            @Override
            public CharSequence getCharContent(boolean ignore) {
                return src;
            }
        };
        JavacTask task = (JavacTask) compiler.getTask(null, null, diags, List.of("-proc:none"), null,
            List.of(file));
        Iterable<? extends CompilationUnitTree> units = task.parse();
        for (Diagnostic<? extends JavaFileObject> d : diags.getDiagnostics()) {
            if (d.getKind() == Diagnostic.Kind.ERROR) {
                fail("parse_error", d.getMessage(null), d.getLineNumber(), d.getColumnNumber());
                return;
            }
        }
        unit = units.iterator().next();
        positions = Trees.instance(task).getSourcePositions();
        docTrees = DocTrees.instance(task);
        collectImports();
        TreePath root = new TreePath(unit);
        for (Tree t : unit.getTypeDecls()) {
            if (t instanceof ClassTree) visitClass(new TreePath(root, t), -1, null, null);
        }
        Map<String, Object> result = new LinkedHashMap<>();
        result.put("ok", true);
        result.put("language", "java");
        result.put("imports", imports);
        result.put("symbols", symbols);
        result.put("exports", exportNames);
        emit(result);
    }
}
