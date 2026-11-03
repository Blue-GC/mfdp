using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace MfdpCSharp
{
    internal sealed class ClassInfo
    {
        public string Qualified;
        public HashSet<string> Members = new HashSet<string>();
        public HashSet<string> TypeParams = new HashSet<string>();
        public Dictionary<string, string> FieldTypes = new Dictionary<string, string>();
        public ClassInfo Outer;
    }

    internal sealed class Uses
    {
        public List<string> Calls = new List<string>();
        public List<string> Refs = new List<string>();
    }

    internal static class Program
    {
        const int MaxSignature = 240;
        const int MaxValue = 60;

        static string src;
        static SyntaxTree tree;
        static readonly List<Dictionary<string, object>> Symbols = new List<Dictionary<string, object>>();
        static readonly List<Dictionary<string, object>> Imports = new List<Dictionary<string, object>>();
        static readonly List<string> ExportNames = new List<string>();
        static readonly HashSet<string> ImportLocals = new HashSet<string>();

        static readonly HashSet<string> Universe = new HashSet<string>
        {
            "System", "Console", "Math", "String", "Int32", "Int64", "Double", "Boolean", "Byte", "Char",
            "Convert", "DateTime", "DateTimeOffset", "TimeSpan", "Guid", "Task", "ValueTask", "List",
            "Dictionary", "HashSet", "Queue", "Stack", "IEnumerable", "IEnumerator", "IList", "ICollection",
            "IDictionary", "Exception", "ArgumentException", "ArgumentNullException", "InvalidOperationException",
            "NotImplementedException", "NotSupportedException", "Func", "Action", "Predicate", "Enumerable",
            "Environment", "Array", "Object", "Nullable", "Span", "ReadOnlySpan", "Stopwatch", "StringBuilder",
            "Tuple", "ValueTuple", "Lazy", "Type", "Attribute", "IDisposable", "IComparable", "IEquatable",
            "EventArgs", "EventHandler", "Random", "Path", "File", "Directory", "Regex", "Linq", "var", "nameof",
            "typeof", "default", "value", "Length", "Count"
        };

        static string Collapse(string s)
        {
            return string.Join(" ", s.Split((char[])null, StringSplitOptions.RemoveEmptyEntries));
        }

        static string Truncate(string s, int limit)
        {
            string c = Collapse(s);
            return c.Length <= limit ? c : c.Substring(0, limit - 3) + "...";
        }

        static string Slice(int start, int end)
        {
            if (start < 0 || end < start || end > src.Length) return "";
            return src.Substring(start, end - start);
        }

        static int LineOf(int pos)
        {
            return tree.GetLineSpan(new Microsoft.CodeAnalysis.Text.TextSpan(Math.Max(pos, 0), 0)).StartLinePosition.Line + 1;
        }

        static string StripGenerics(string s)
        {
            int i = s.IndexOf('<');
            return (i >= 0 ? s.Substring(0, i) : s).Trim();
        }

        static SyntaxTokenList ModsOf(MemberDeclarationSyntax m)
        {
            switch (m)
            {
                case BaseTypeDeclarationSyntax t: return t.Modifiers;
                case BaseMethodDeclarationSyntax x: return x.Modifiers;
                case BasePropertyDeclarationSyntax x: return x.Modifiers;
                case BaseFieldDeclarationSyntax x: return x.Modifiers;
                case DelegateDeclarationSyntax x: return x.Modifiers;
                default: return default(SyntaxTokenList);
            }
        }

        static bool HasMod(SyntaxTokenList mods, SyntaxKind kind)
        {
            foreach (var t in mods)
            {
                if (t.IsKind(kind)) return true;
            }
            return false;
        }

        static string ExportedFlag(MemberDeclarationSyntax m, bool topLevel, bool inInterface)
        {
            var mods = ModsOf(m);
            if (HasMod(mods, SyntaxKind.PublicKeyword)) return "yes";
            if (HasMod(mods, SyntaxKind.PrivateKeyword)) return "no";
            if (HasMod(mods, SyntaxKind.ProtectedKeyword) || HasMod(mods, SyntaxKind.InternalKeyword)) return "unknown";
            if (inInterface) return "unknown";
            return "no";
        }

        static int HeaderStart(MemberDeclarationSyntax m)
        {
            if (m.AttributeLists.Count > 0) return m.AttributeLists.Last().Span.End;
            return m.SpanStart;
        }

        static string DocOf(SyntaxNode node)
        {
            string best = "";
            foreach (var t in node.GetLeadingTrivia())
            {
                if (t.IsKind(SyntaxKind.SingleLineDocumentationCommentTrivia) ||
                    t.IsKind(SyntaxKind.MultiLineDocumentationCommentTrivia))
                {
                    best = t.ToString();
                }
            }
            if (best.Length == 0) return "";
            var lines = new List<string>();
            foreach (string raw in best.Split('\n'))
            {
                string line = raw.Trim();
                if (line.StartsWith("///")) line = line.Substring(3);
                else if (line.StartsWith("/**")) line = line.Substring(3);
                else if (line.StartsWith("*/")) line = "";
                else if (line.StartsWith("*")) line = line.Substring(1);
                line = Regex.Replace(line, "<[^>]+>", " ");
                line = line.Trim();
                if (line.Length > 0) lines.Add(line);
            }
            return string.Join("\n", lines);
        }

        static string Dotted(ExpressionSyntax e)
        {
            switch (e)
            {
                case IdentifierNameSyntax id: return id.Identifier.Text;
                case GenericNameSyntax g: return g.Identifier.Text;
                case ThisExpressionSyntax _: return "this";
                case BaseExpressionSyntax _: return "base";
                case PredefinedTypeSyntax p: return p.Keyword.Text;
                case ParenthesizedExpressionSyntax p: return Dotted(p.Expression);
                case QualifiedNameSyntax q:
                    {
                        string left = Dotted(q.Left);
                        string right = Dotted(q.Right);
                        return left == null || right == null ? null : left + "." + right;
                    }
                case MemberAccessExpressionSyntax m:
                    {
                        string b = Dotted(m.Expression);
                        string n = Dotted(m.Name);
                        return b == null || n == null ? null : b + "." + n;
                    }
                case NullableTypeSyntax n: return Dotted(n.ElementType);
                default: return null;
            }
        }

        static string TypeName(TypeSyntax t)
        {
            if (t == null || t is PredefinedTypeSyntax || t is ArrayTypeSyntax) return null;
            if (t is IdentifierNameSyntax idn && idn.Identifier.Text == "var") return null;
            return Dotted(t);
        }

        static string RootOf(string name)
        {
            int dot = name.IndexOf('.');
            return dot < 0 ? name : name.Substring(0, dot);
        }

        static bool IsTypeParam(ClassInfo info, string name)
        {
            for (var c = info; c != null; c = c.Outer)
            {
                if (c.TypeParams.Contains(name)) return true;
            }
            return false;
        }

        static List<string> Normalize(List<string> names, HashSet<string> locals, Dictionary<string, string> localTypes,
            ClassInfo info, bool isCall)
        {
            var output = new List<string>();
            foreach (string n in names)
            {
                int dot = n.IndexOf('.');
                string root = dot < 0 ? n : n.Substring(0, dot);
                string result = n;
                if (root == "this")
                {
                    if (n == "this") continue;
                    string[] parts = n.Split(new[] { '.' }, 3);
                    if (parts.Length == 3 && info != null && info.FieldTypes.TryGetValue(parts[1], out string ft))
                    {
                        if (Universe.Contains(RootOf(ft)) || IsTypeParam(info, RootOf(ft))) continue;
                        result = ft + "." + parts[2];
                    }
                }
                else if (locals.Contains(root))
                {
                    if (localTypes.TryGetValue(root, out string lt) && dot >= 0 && !Universe.Contains(RootOf(lt)) &&
                        !IsTypeParam(info, RootOf(lt)))
                    {
                        string typed = lt + n.Substring(dot);
                        if (!output.Contains(typed)) output.Add(typed);
                    }
                    continue;
                }
                else if (root == "base" || IsTypeParam(info, root))
                {
                    continue;
                }
                else if (Universe.Contains(root) && !(info != null && info.Members.Contains(root)))
                {
                    continue;
                }
                else
                {
                    ClassInfo owner = null;
                    for (var c = info; c != null; c = c.Outer)
                    {
                        if (c.Members.Contains(root))
                        {
                            owner = c;
                            break;
                        }
                    }
                    string fieldType = null;
                    if (owner != null) owner.FieldTypes.TryGetValue(root, out fieldType);
                    if (fieldType != null && dot >= 0)
                    {
                        if (Universe.Contains(RootOf(fieldType)) || IsTypeParam(owner, RootOf(fieldType))) continue;
                        result = fieldType + n.Substring(dot);
                    }
                    else if (owner != null && owner == info) result = "this." + n;
                    else if (owner != null) result = owner.Qualified + "." + n;
                    else if (isCall && dot < 0 && !ImportLocals.Contains(root) && info != null) result = "this." + n;
                }
                if (!output.Contains(result)) output.Add(result);
            }
            return output;
        }

        static Uses ComputeUses(IEnumerable<SyntaxNode> roots, ClassInfo info)
        {
            var locals = new HashSet<string>();
            var localTypes = new Dictionary<string, string>();
            var calls = new List<string>();
            var loads = new List<string>();
            var rootList = roots.Where(r => r != null).ToList();
            foreach (var root in rootList)
            {
                foreach (var n in root.DescendantNodesAndSelf())
                {
                    switch (n)
                    {
                        case VariableDeclaratorSyntax v:
                            locals.Add(v.Identifier.Text);
                            if (v.Parent is VariableDeclarationSyntax vd)
                            {
                                string vt = TypeName(vd.Type);
                                if (vt != null) localTypes[v.Identifier.Text] = vt;
                            }
                            break;
                        case ParameterSyntax p:
                            locals.Add(p.Identifier.Text);
                            {
                                string pt = TypeName(p.Type);
                                if (pt != null) localTypes[p.Identifier.Text] = pt;
                            }
                            break;
                        case ForEachStatementSyntax f:
                            locals.Add(f.Identifier.Text);
                            {
                                string ft2 = TypeName(f.Type);
                                if (ft2 != null) localTypes[f.Identifier.Text] = ft2;
                            }
                            break;
                        case CatchDeclarationSyntax c: locals.Add(c.Identifier.Text); break;
                        case TypeParameterSyntax tp: locals.Add(tp.Identifier.Text); break;
                        case LocalFunctionStatementSyntax lf: locals.Add(lf.Identifier.Text); break;
                        case SingleVariableDesignationSyntax sv: locals.Add(sv.Identifier.Text); break;
                    }
                }
            }
            foreach (var root in rootList)
            {
                foreach (var n in root.DescendantNodesAndSelf())
                {
                    switch (n)
                    {
                        case InvocationExpressionSyntax inv:
                            {
                                string d = Dotted(inv.Expression);
                                if (d != null) calls.Add(d);
                                break;
                            }
                        case ObjectCreationExpressionSyntax oc:
                            {
                                string d = Dotted(oc.Type);
                                if (d != null) calls.Add(d);
                                break;
                            }
                        case MemberAccessExpressionSyntax ma:
                            {
                                string d = Dotted(ma);
                                if (d != null && d.StartsWith("this.")) loads.Add(d);
                                break;
                            }
                        case IdentifierNameSyntax idn:
                            AddLoad(idn, idn.Identifier.Text, loads);
                            break;
                        case GenericNameSyntax gn:
                            AddLoad(gn, gn.Identifier.Text, loads);
                            break;
                    }
                }
            }
            var u = new Uses();
            u.Calls = Normalize(calls, locals, localTypes, info, true);
            u.Refs = Normalize(loads, locals, localTypes, info, false);
            return u;
        }

        static void AddLoad(SyntaxNode node, string name, List<string> loads)
        {
            var parent = node.Parent;
            if (parent is MemberAccessExpressionSyntax ma && ma.Name == node) return;
            if (parent is QualifiedNameSyntax q && q.Right == node) return;
            if (parent is NameColonSyntax || parent is NameEqualsSyntax || parent is MemberBindingExpressionSyntax) return;
            if (parent is AssignmentExpressionSyntax ae && ae.Left == node && ae.Parent is InitializerExpressionSyntax) return;
            if (parent is ArgumentSyntax arg && arg.NameColon != null && arg.NameColon.Name == node) return;
            if (name == "var" || name == "nameof") return;
            loads.Add(name);
        }

        static int AddSymbol(string kind, int parent, string name, string qualified, string signature,
            List<Dictionary<string, object>> parameters, string returns, List<string> decorators,
            List<string> bases, string doc, int start, int end, string exported, Uses uses)
        {
            var s = new Dictionary<string, object>
            {
                ["kind"] = kind,
                ["parent"] = parent,
                ["name"] = name,
                ["qualified"] = qualified,
                ["signature"] = signature,
                ["params"] = parameters,
                ["returns"] = returns,
                ["decorators"] = decorators,
                ["bases"] = bases,
                ["doc"] = doc,
                ["startLine"] = LineOf(start),
                ["endLine"] = LineOf(Math.Max(start, end - 1)),
                ["exported"] = exported,
                ["calls"] = uses == null ? new List<string>() : uses.Calls,
                ["refs"] = uses == null ? new List<string>() : uses.Refs,
                ["source"] = Slice(start, end),
                ["defaultExport"] = false
            };
            Symbols.Add(s);
            return Symbols.Count - 1;
        }

        static List<Dictionary<string, object>> ParamsOf(ParameterListSyntax list)
        {
            var result = new List<Dictionary<string, object>>();
            if (list == null) return result;
            foreach (var p in list.Parameters)
            {
                string mods = p.Modifiers.Count > 0 ? string.Join(" ", p.Modifiers.Select(m => m.Text)) + " " : "";
                result.Add(new Dictionary<string, object>
                {
                    ["name"] = mods + p.Identifier.Text,
                    ["annotation"] = p.Type == null ? "" : Collapse(p.Type.ToString()),
                    ["default"] = p.Default == null ? "" : Collapse(p.Default.Value.ToString())
                });
            }
            return result;
        }

        static List<string> AttributesOf(MemberDeclarationSyntax m)
        {
            var result = new List<string>();
            foreach (var list in m.AttributeLists) result.Add(Collapse(list.ToString()));
            return result;
        }

        static ClassInfo InfoFor(MemberDeclarationSyntax type, ClassInfo outer, string qualified)
        {
            var ci = new ClassInfo { Qualified = qualified, Outer = outer };
            if (type is TypeDeclarationSyntax td)
            {
                if (td.TypeParameterList != null)
                {
                    foreach (var tp in td.TypeParameterList.Parameters) ci.TypeParams.Add(tp.Identifier.Text);
                }
                if (td.ParameterList != null)
                {
                    foreach (var p in td.ParameterList.Parameters) ci.Members.Add(p.Identifier.Text);
                }
                foreach (var m in td.Members) AddMemberNames(m, ci);
            }
            else if (type is EnumDeclarationSyntax ed)
            {
                foreach (var m in ed.Members) ci.Members.Add(m.Identifier.Text);
            }
            return ci;
        }

        static void AddMemberNames(MemberDeclarationSyntax m, ClassInfo ci)
        {
            switch (m)
            {
                case MethodDeclarationSyntax md: ci.Members.Add(md.Identifier.Text); break;
                case PropertyDeclarationSyntax pd:
                    ci.Members.Add(pd.Identifier.Text);
                    {
                        string ptn = TypeName(pd.Type);
                        if (ptn != null) ci.FieldTypes[pd.Identifier.Text] = ptn;
                    }
                    break;
                case EventDeclarationSyntax ev: ci.Members.Add(ev.Identifier.Text); break;
                case BaseFieldDeclarationSyntax fd:
                    {
                        string ftn = TypeName(fd.Declaration.Type);
                        foreach (var v in fd.Declaration.Variables)
                        {
                            ci.Members.Add(v.Identifier.Text);
                            if (ftn != null) ci.FieldTypes[v.Identifier.Text] = ftn;
                        }
                    }
                    break;
                case BaseTypeDeclarationSyntax td: ci.Members.Add(td.Identifier.Text); break;
            }
        }

        static void VisitMembers(IEnumerable<MemberDeclarationSyntax> members, int parent, ClassInfo info,
            string prefix, bool inInterface)
        {
            foreach (var member in members) VisitMember(member, parent, info, prefix, inInterface);
        }

        static void VisitMember(MemberDeclarationSyntax m, int parent, ClassInfo info, string prefix, bool inInterface)
        {
            bool topLevel = parent < 0 || (parent >= 0 && (string)Symbols[parent]["kind"] == "namespace");
            string Q(string name) => prefix.Length == 0 ? name : prefix + "." + name;
            int hs = HeaderStart(m);
            int end = m.Span.End;
            switch (m)
            {
                case BaseNamespaceDeclarationSyntax ns:
                    {
                        string name = ns.Name.ToString();
                        int idx = AddSymbol("namespace", parent, name, Q(name), "namespace " + name,
                            new List<Dictionary<string, object>>(), "", new List<string>(), new List<string>(),
                            DocOf(ns), m.SpanStart, end, "unknown", null);
                        VisitMembers(ns.Members, idx, info, Q(name), false);
                        break;
                    }
                case TypeDeclarationSyntax td:
                    {
                        string name = td.Identifier.Text;
                        string qualified = Q(name);
                        var ci = InfoFor(td, info, qualified);
                        int headEnd = td.OpenBraceToken.IsMissing ? end : td.OpenBraceToken.SpanStart;
                        string head = Truncate(Slice(hs, headEnd).TrimEnd(';'), MaxSignature);
                        var bases = new List<string>();
                        if (td.BaseList != null)
                        {
                            foreach (var t in td.BaseList.Types) bases.Add(StripGenerics(Collapse(t.Type.ToString())));
                        }
                        var roots = new List<SyntaxNode>();
                        foreach (var al in td.AttributeLists) roots.Add(al);
                        if (td.BaseList != null) roots.Add(td.BaseList);
                        foreach (var cc in td.ConstraintClauses) roots.Add(cc);
                        var uses = ComputeUses(roots, ci);
                        bool isInterface = td is InterfaceDeclarationSyntax;
                        int idx = AddSymbol(isInterface ? "interface" : "class", parent, name, qualified, head,
                            new List<Dictionary<string, object>>(), "", AttributesOf(td), bases, DocOf(td),
                            m.SpanStart, end, ExportedFlag(td, topLevel, inInterface), uses);
                        if (topLevel && HasMod(td.Modifiers, SyntaxKind.PublicKeyword)) ExportNames.Add(name);
                        if (td.ParameterList != null)
                        {
                            foreach (var p in td.ParameterList.Parameters)
                            {
                                AddSymbol("property", idx, p.Identifier.Text, qualified + "." + p.Identifier.Text,
                                    Collapse(p.ToString()), new List<Dictionary<string, object>>(),
                                    p.Type == null ? "" : Collapse(p.Type.ToString()), new List<string>(),
                                    new List<string>(), "", p.SpanStart, p.Span.End, "unknown", null);
                            }
                        }
                        VisitMembers(td.Members, idx, ci, qualified, isInterface);
                        break;
                    }
                case EnumDeclarationSyntax ed:
                    {
                        string name = ed.Identifier.Text;
                        int headEnd = ed.OpenBraceToken.IsMissing ? end : ed.OpenBraceToken.SpanStart;
                        string names = string.Join(", ", ed.Members.Select(x => x.Identifier.Text));
                        string sig = Truncate(Slice(hs, headEnd) + " { " + names + " }", MaxSignature);
                        var roots = new List<SyntaxNode>();
                        foreach (var al in ed.AttributeLists) roots.Add(al);
                        foreach (var em in ed.Members)
                        {
                            if (em.EqualsValue != null) roots.Add(em.EqualsValue);
                        }
                        AddSymbol("enum", parent, name, Q(name), sig, new List<Dictionary<string, object>>(), "",
                            AttributesOf(ed), new List<string>(), DocOf(ed), m.SpanStart, end,
                            ExportedFlag(ed, topLevel, inInterface), ComputeUses(roots, info));
                        if (topLevel && HasMod(ed.Modifiers, SyntaxKind.PublicKeyword)) ExportNames.Add(name);
                        break;
                    }
                case DelegateDeclarationSyntax dd:
                    {
                        AddSymbol("type", parent, dd.Identifier.Text, Q(dd.Identifier.Text),
                            Truncate(Slice(hs, end).TrimEnd(';'), MaxSignature), ParamsOf(dd.ParameterList),
                            Collapse(dd.ReturnType.ToString()), AttributesOf(dd), new List<string>(), DocOf(dd),
                            m.SpanStart, end, ExportedFlag(dd, topLevel, inInterface),
                            ComputeUses(new SyntaxNode[] { dd }, info));
                        break;
                    }
                case MethodDeclarationSyntax md:
                    {
                        int headEnd = md.Body != null ? md.Body.SpanStart
                            : md.ExpressionBody != null ? md.ExpressionBody.ArrowToken.SpanStart : end;
                        AddSymbol("method", parent, md.Identifier.Text, Q(md.Identifier.Text),
                            Truncate(Slice(hs, headEnd).TrimEnd(';'), MaxSignature), ParamsOf(md.ParameterList),
                            Collapse(md.ReturnType.ToString()), AttributesOf(md), new List<string>(), DocOf(md),
                            m.SpanStart, end, ExportedFlag(md, false, inInterface),
                            ComputeUses(new SyntaxNode[] { md }, info));
                        break;
                    }
                case ConstructorDeclarationSyntax cd:
                    {
                        int headEnd = cd.Body != null ? cd.Body.SpanStart
                            : cd.ExpressionBody != null ? cd.ExpressionBody.ArrowToken.SpanStart : end;
                        AddSymbol("constructor", parent, cd.Identifier.Text, Q(cd.Identifier.Text),
                            Truncate(Slice(hs, headEnd).TrimEnd(';'), MaxSignature), ParamsOf(cd.ParameterList), "",
                            AttributesOf(cd), new List<string>(), DocOf(cd), m.SpanStart, end,
                            ExportedFlag(cd, false, inInterface), ComputeUses(new SyntaxNode[] { cd }, info));
                        break;
                    }
                case DestructorDeclarationSyntax dd:
                    {
                        string name = "~" + dd.Identifier.Text;
                        AddSymbol("method", parent, name, Q(name), Truncate(Slice(hs, dd.Body != null ? dd.Body.SpanStart : end), MaxSignature),
                            new List<Dictionary<string, object>>(), "", AttributesOf(dd), new List<string>(), DocOf(dd),
                            m.SpanStart, end, "no", ComputeUses(new SyntaxNode[] { dd }, info));
                        break;
                    }
                case OperatorDeclarationSyntax od:
                    {
                        string name = "operator " + od.OperatorToken.Text;
                        int headEnd = od.Body != null ? od.Body.SpanStart
                            : od.ExpressionBody != null ? od.ExpressionBody.ArrowToken.SpanStart : end;
                        AddSymbol("method", parent, name, Q(name), Truncate(Slice(hs, headEnd).TrimEnd(';'), MaxSignature),
                            ParamsOf(od.ParameterList), Collapse(od.ReturnType.ToString()), AttributesOf(od),
                            new List<string>(), DocOf(od), m.SpanStart, end, ExportedFlag(od, false, inInterface),
                            ComputeUses(new SyntaxNode[] { od }, info));
                        break;
                    }
                case PropertyDeclarationSyntax pd:
                    {
                        int headEnd = pd.AccessorList != null ? pd.AccessorList.SpanStart
                            : pd.ExpressionBody != null ? pd.ExpressionBody.ArrowToken.SpanStart : end;
                        AddSymbol("property", parent, pd.Identifier.Text, Q(pd.Identifier.Text),
                            Truncate(Slice(hs, headEnd).TrimEnd(';'), MaxSignature), new List<Dictionary<string, object>>(),
                            Collapse(pd.Type.ToString()), AttributesOf(pd), new List<string>(), DocOf(pd),
                            m.SpanStart, end, ExportedFlag(pd, false, inInterface),
                            ComputeUses(new SyntaxNode[] { pd }, info));
                        break;
                    }
                case IndexerDeclarationSyntax ix:
                    {
                        AddSymbol("property", parent, "this[]", Q("this[]"),
                            Truncate(Slice(hs, ix.AccessorList != null ? ix.AccessorList.SpanStart : end).TrimEnd(';'), MaxSignature),
                            ParamsOf(null), Collapse(ix.Type.ToString()), AttributesOf(ix), new List<string>(), DocOf(ix),
                            m.SpanStart, end, ExportedFlag(ix, false, inInterface),
                            ComputeUses(new SyntaxNode[] { ix }, info));
                        break;
                    }
                case EventDeclarationSyntax ev:
                    {
                        AddSymbol("property", parent, ev.Identifier.Text, Q(ev.Identifier.Text),
                            Truncate(Slice(hs, ev.AccessorList != null ? ev.AccessorList.SpanStart : end), MaxSignature),
                            new List<Dictionary<string, object>>(), Collapse(ev.Type.ToString()), AttributesOf(ev),
                            new List<string>(), DocOf(ev), m.SpanStart, end, ExportedFlag(ev, false, inInterface), null);
                        break;
                    }
                case BaseFieldDeclarationSyntax fd:
                    {
                        var mods = fd.Modifiers;
                        bool constant = HasMod(mods, SyntaxKind.ConstKeyword) ||
                            (HasMod(mods, SyntaxKind.StaticKeyword) && HasMod(mods, SyntaxKind.ReadOnlyKeyword));
                        string typeText = Collapse(fd.Declaration.Type.ToString());
                        string modText = mods.Count > 0 ? string.Join(" ", mods.Select(t => t.Text)) + " " : "";
                        foreach (var v in fd.Declaration.Variables)
                        {
                            string sig = modText + typeText + " " + v.Identifier.Text;
                            if (v.Initializer != null) sig += " = " + Truncate(v.Initializer.Value.ToString(), MaxValue);
                            AddSymbol(constant ? "constant" : "property", parent, v.Identifier.Text,
                                Q(v.Identifier.Text), Truncate(sig, MaxSignature),
                                new List<Dictionary<string, object>>(), typeText, AttributesOf(fd), new List<string>(),
                                DocOf(fd), m.SpanStart, end, ExportedFlag(fd, false, inInterface),
                                ComputeUses(new SyntaxNode[] { v }, info));
                        }
                        break;
                    }
            }
        }

        static void CollectUsings(SyntaxNode root)
        {
            foreach (var u in root.DescendantNodes().OfType<UsingDirectiveSyntax>())
            {
                if (u.Name == null) continue;
                int s = u.SpanStart;
                int e = u.Span.End;
                var entry = new Dictionary<string, object>
                {
                    ["line"] = LineOf(s),
                    ["endLine"] = LineOf(Math.Max(s, e - 1)),
                    ["text"] = Collapse(Slice(s, e))
                };
                var bindings = new List<Dictionary<string, object>>();
                string full = u.Name.ToString();
                if (u.Alias != null)
                {
                    string alias = u.Alias.Name.ToString();
                    int dot = full.LastIndexOf('.');
                    string simple = dot < 0 ? full : full.Substring(dot + 1);
                    string module = dot < 0 ? full : full.Substring(0, dot);
                    bindings.Add(new Dictionary<string, object>
                    {
                        ["local"] = alias,
                        ["imported"] = simple,
                        ["module"] = module,
                        ["depth"] = 1
                    });
                    ImportLocals.Add(alias);
                    entry["module"] = module;
                    entry["star"] = false;
                }
                else
                {
                    entry["module"] = full;
                    entry["star"] = true;
                }
                entry["bindings"] = bindings;
                Imports.Add(entry);
            }
        }

        static void Emit(Dictionary<string, object> result)
        {
            Console.Out.WriteLine(JsonSerializer.Serialize(result));
            Console.Out.Flush();
        }

        static void Fail(string kind, string message, int line, int column)
        {
            Emit(new Dictionary<string, object>
            {
                ["ok"] = false,
                ["error"] = new Dictionary<string, object>
                {
                    ["kind"] = kind,
                    ["message"] = message,
                    ["line"] = line,
                    ["column"] = column
                }
            });
        }

        static int Main(string[] args)
        {
            byte[] data;
            using (var stdin = Console.OpenStandardInput())
            using (var ms = new MemoryStream())
            {
                stdin.CopyTo(ms);
                data = ms.ToArray();
            }
            try
            {
                src = new UTF8Encoding(false, true).GetString(data);
            }
            catch (Exception)
            {
                Fail("parse_error", "source is not valid UTF-8", 0, 0);
                return 0;
            }
            if (src.Length > 0 && src[0] == '\uFEFF') src = src.Substring(1);
            tree = CSharpSyntaxTree.ParseText(src, new CSharpParseOptions(LanguageVersion.Latest));
            foreach (var d in tree.GetDiagnostics())
            {
                if (d.Severity != DiagnosticSeverity.Error) continue;
                var pos = d.Location.GetLineSpan().StartLinePosition;
                Fail("parse_error", d.GetMessage(), pos.Line + 1, pos.Character + 1);
                return 0;
            }
            var root = (CompilationUnitSyntax)tree.GetRoot();
            CollectUsings(root);
            VisitMembers(root.Members, -1, null, "", false);
            Emit(new Dictionary<string, object>
            {
                ["ok"] = true,
                ["language"] = "csharp",
                ["imports"] = Imports,
                ["symbols"] = Symbols,
                ["exports"] = ExportNames
            });
            return 0;
        }
    }
}
