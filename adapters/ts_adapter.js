'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const childProcess = require('child_process');

const MAX_SIGNATURE_TEXT = 240;
const MAX_VALUE_TEXT = 60;

const TYPE_GLOBALS = [
  'Record', 'Partial', 'Required', 'Readonly', 'Pick', 'Omit', 'Exclude', 'Extract', 'NonNullable',
  'ReturnType', 'Parameters', 'ConstructorParameters', 'InstanceType', 'Awaited', 'ReadonlyArray',
  'ThisType', 'ThisParameterType', 'OmitThisParameter', 'Uppercase', 'Lowercase', 'Capitalize',
  'Uncapitalize', 'PropertyKey', 'ArrayLike', 'Iterable', 'Iterator', 'IterableIterator',
  'AsyncIterable', 'AsyncIterator', 'AsyncIterableIterator', 'PromiseLike', 'TemplateStringsArray',
  'ReadonlyMap', 'ReadonlySet', 'NoInfer', 'arguments', 'require', 'module', 'exports', '__dirname',
  '__filename'
];

function emit(obj) {
  process.stdout.write(JSON.stringify(obj));
}

function failure(kind, message, line, column) {
  return { ok: false, error: { kind, message, line: line || 0, column: column || 0 } };
}

let incompatibleFound = false;

function loadTypescript() {
  const attempt = (spec) => {
    try {
      const mod = require(spec);
      if (mod && typeof mod.createSourceFile === 'function' && typeof mod.createProgram === 'function' && mod.ScriptKind) {
        return mod;
      }
      incompatibleFound = true;
      return null;
    } catch (e) {
      return null;
    }
  };
  if (process.env.MFDP_TYPESCRIPT) {
    const found = attempt(process.env.MFDP_TYPESCRIPT);
    if (found) return found;
  }
  let found = attempt('typescript');
  if (found) return found;
  const roots = [];
  if (process.env.NODE_PATH) {
    roots.push(...process.env.NODE_PATH.split(path.delimiter).filter(Boolean));
  }
  roots.push(path.join(path.dirname(process.execPath), '..', 'lib', 'node_modules'));
  roots.push('/usr/lib/node_modules', '/usr/local/lib/node_modules');
  roots.push(path.join(os.homedir(), '.npm-global', 'lib', 'node_modules'));
  for (const root of roots) {
    found = attempt(path.join(root, 'typescript'));
    if (found) return found;
  }
  try {
    const globalRoot = childProcess
      .execFileSync('npm', ['root', '-g'], {
        cwd: os.tmpdir(),
        encoding: 'utf8',
        timeout: 15000,
        stdio: ['ignore', 'pipe', 'ignore']
      })
      .trim();
    if (globalRoot) {
      found = attempt(path.join(globalRoot, 'typescript'));
      if (found) return found;
    }
  } catch (e) {
    return null;
  }
  return null;
}

function parseArgs(argv) {
  const out = { language: 'typescript', filename: 'source.ts' };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--language' && i + 1 < argv.length) out.language = argv[++i];
    else if (argv[i] === '--filename' && i + 1 < argv.length) out.filename = argv[++i];
  }
  return out;
}

function chooseVirtualName(ts, language, filename) {
  const ext = path.extname(filename).toLowerCase();
  if (language === 'typescript') {
    return ext === '.tsx'
      ? { name: 'source.tsx', kind: ts.ScriptKind.TSX }
      : { name: 'source.ts', kind: ts.ScriptKind.TS };
  }
  return ext === '.jsx'
    ? { name: 'source.jsx', kind: ts.ScriptKind.JSX }
    : { name: 'source.js', kind: ts.ScriptKind.JS };
}

function collapse(text) {
  return text.replace(/\s+/g, ' ').trim();
}

function truncate(text, limit) {
  const flat = collapse(text);
  return flat.length <= limit ? flat : flat.slice(0, limit - 3) + '...';
}

function analyze(ts, text, language, filename) {
  const virtual = chooseVirtualName(ts, language, filename);
  const sf = ts.createSourceFile(virtual.name, text, ts.ScriptTarget.Latest, true, virtual.kind);
  const host = {
    getSourceFile: (name) => (name === virtual.name ? sf : undefined),
    fileExists: (name) => name === virtual.name,
    readFile: () => undefined,
    getDefaultLibFileName: () => 'lib.d.ts',
    writeFile: () => {},
    getCurrentDirectory: () => '/',
    getCanonicalFileName: (name) => name,
    useCaseSensitiveFileNames: () => true,
    getNewLine: () => '\n',
    getDirectories: () => [],
    directoryExists: () => true
  };
  const program = ts.createProgram(
    [virtual.name],
    { noLib: true, noResolve: true, allowJs: true, checkJs: false, target: ts.ScriptTarget.Latest },
    host
  );
  const diagnostics = program
    .getSyntacticDiagnostics(sf)
    .filter((d) => d.category === ts.DiagnosticCategory.Error);
  if (diagnostics.length > 0) {
    const d = diagnostics[0];
    const pos = sf.getLineAndCharacterOfPosition(d.start || 0);
    return failure(
      'parse_error',
      ts.flattenDiagnosticMessageText(d.messageText, '\n'),
      pos.line + 1,
      pos.character + 1
    );
  }

  const globalNames = new Set(Object.getOwnPropertyNames(globalThis));
  for (const name of TYPE_GLOBALS) globalNames.add(name);

  const symbols = [];
  const imports = [];
  const exportNames = [];
  const exportedLocal = new Set();
  let defaultName = null;
  const declaredTop = new Set();

  const K = ts.SyntaxKind;

  function lineOf(pos) {
    return sf.getLineAndCharacterOfPosition(pos).line + 1;
  }

  function hasModifier(node, kind) {
    const mods = ts.canHaveModifiers(node) ? ts.getModifiers(node) : undefined;
    return !!mods && mods.some((m) => m.kind === kind);
  }

  function decoratorsOf(node) {
    const list = ts.canHaveDecorators(node) ? ts.getDecorators(node) : undefined;
    return list ? list.map((d) => collapse(d.getText(sf))) : [];
  }

  function docOf(node) {
    const ranges = ts.getLeadingCommentRanges(text, node.getFullStart()) || [];
    let found = null;
    for (const r of ranges) {
      if (r.kind === K.MultiLineCommentTrivia && text.startsWith('/**', r.pos)) found = r;
    }
    if (!found) return '';
    return text
      .slice(found.pos + 3, found.end - 2)
      .split('\n')
      .map((line) => line.replace(/^\s*\*\s?/, '').trimEnd())
      .join('\n')
      .trim();
  }

  function dottedName(expr) {
    const parts = [];
    let cur = expr;
    for (;;) {
      if (ts.isParenthesizedExpression(cur) || ts.isNonNullExpression(cur) || ts.isAsExpression(cur)) {
        cur = cur.expression;
      } else if (ts.isPropertyAccessExpression(cur)) {
        parts.push(cur.name.text);
        cur = cur.expression;
      } else if (ts.isIdentifier(cur)) {
        parts.push(cur.text);
        break;
      } else if (cur.kind === K.ThisKeyword) {
        parts.push('this');
        break;
      } else if (cur.kind === K.SuperKeyword) {
        parts.push('super');
        break;
      } else {
        return null;
      }
    }
    return parts.reverse().join('.');
  }

  function bindingNames(nameNode, into) {
    if (!nameNode) return;
    if (ts.isIdentifier(nameNode)) {
      into.push(nameNode.text);
    } else if (ts.isObjectBindingPattern(nameNode) || ts.isArrayBindingPattern(nameNode)) {
      for (const el of nameNode.elements) {
        if (ts.isBindingElement(el)) bindingNames(el.name, into);
      }
    }
  }

  function isReference(id) {
    const p = id.parent;
    if (!p) return true;
    if (ts.isPropertyAccessExpression(p) && p.name === id) return false;
    if (ts.isQualifiedName(p) && p.right === id) return false;
    if (ts.isShorthandPropertyAssignment(p)) return false;
    if (ts.isTypeReferenceNode(p) && p.typeName === id) return true;
    if (ts.isExpressionWithTypeArguments(p) && p.expression === id) return true;
    if (ts.isTypeQueryNode(p) && p.exprName === id) return true;
    if (p.name === id) return false;
    if (ts.isBindingElement(p) && p.propertyName === id) return false;
    if ((ts.isImportSpecifier(p) || ts.isExportSpecifier(p)) && p.propertyName === id) return false;
    if (ts.isLabeledStatement(p) || ts.isBreakOrContinueStatement(p)) return false;
    if (ts.isJsxAttribute(p)) return false;
    if (ts.isMetaProperty(p)) return false;
    return true;
  }

  function gather(roots, skipMembers) {
    const locals = new Set();
    const loads = [];
    const calls = [];
    function visit(node) {
      if (skipMembers && (ts.isMethodDeclaration(node) || ts.isConstructorDeclaration(node) ||
          ts.isGetAccessorDeclaration(node) || ts.isSetAccessorDeclaration(node) ||
          ts.isPropertyDeclaration(node) || ts.isMethodSignature(node) || ts.isPropertySignature(node) ||
          ts.isClassStaticBlockDeclaration(node) || ts.isIndexSignatureDeclaration(node) ||
          ts.isCallSignatureDeclaration(node) || ts.isConstructSignatureDeclaration(node))) {
        return;
      }
      switch (node.kind) {
        case K.Parameter:
        case K.VariableDeclaration: {
          const names = [];
          bindingNames(node.name, names);
          for (const n of names) locals.add(n);
          break;
        }
        case K.FunctionDeclaration:
        case K.FunctionExpression:
        case K.ClassDeclaration:
        case K.ClassExpression:
        case K.EnumDeclaration:
        case K.ModuleDeclaration:
          if (node.name && ts.isIdentifier(node.name) && !roots.includes(node)) locals.add(node.name.text);
          break;
        case K.TypeParameter:
          locals.add(node.name.text);
          break;
        case K.CallExpression:
        case K.NewExpression: {
          const name = dottedName(node.expression);
          if (name) calls.push(name);
          break;
        }
        case K.Decorator: {
          const name = dottedName(node.expression);
          if (name) calls.push(name);
          break;
        }
        case K.ShorthandPropertyAssignment:
          loads.push(node.name.text);
          break;
        case K.JsxOpeningElement:
        case K.JsxSelfClosingElement: {
          const name = dottedName(node.tagName);
          if (name && /^[A-Z]/.test(name)) loads.push(name.split('.')[0]);
          break;
        }
        case K.Identifier:
          if (isReference(node)) loads.push(node.text);
          break;
        default:
          break;
      }
      ts.forEachChild(node, visit);
    }
    for (const r of roots) {
      if (r) visit(r);
    }
    return { locals, loads, calls };
  }

  function finishUses(uses) {
    const calls = [];
    for (const c of uses.calls) {
      const root = c.split('.')[0];
      if (root === 'this' || root === 'super' || !uses.locals.has(root)) {
        if (!calls.includes(c)) calls.push(c);
      }
    }
    const refs = [];
    for (const r of uses.loads) {
      if (!uses.locals.has(r) && !refs.includes(r)) refs.push(r);
    }
    return { calls, refs };
  }

  function headerText(node, endPos) {
    return collapse(text.slice(node.getStart(sf), endPos));
  }

  function paramsOf(node) {
    return (node.parameters || []).map((p) => {
      const mods = ts.canHaveModifiers(p) ? ts.getModifiers(p) : undefined;
      let name = (mods ? mods.map((m) => m.getText(sf)).join(' ') + ' ' : '') +
        (p.dotDotDotToken ? '...' : '') + p.name.getText(sf) + (p.questionToken ? '?' : '');
      return {
        name,
        annotation: p.type ? collapse(p.type.getText(sf)) : '',
        default: p.initializer ? collapse(p.initializer.getText(sf)) : ''
      };
    });
  }

  function exportedFlag(node, parent, name) {
    if (parent >= 0 && symbols[parent].kind !== 'namespace') return 'unknown';
    if (hasModifier(node, K.ExportKeyword)) return 'yes';
    if (parent < 0 && exportedLocal.has(name)) return 'yes';
    return 'no';
  }

  function addSymbol(spec) {
    const parent = spec.parent;
    const qualified = parent < 0 ? spec.name : symbols[parent].qualified + '.' + spec.name;
    symbols.push({
      kind: spec.kind,
      parent,
      name: spec.name,
      qualified,
      signature: spec.signature,
      params: spec.params || [],
      returns: spec.returns || '',
      decorators: spec.decorators || [],
      bases: spec.bases || [],
      doc: spec.doc || '',
      startLine: spec.startLine,
      endLine: spec.endLine,
      exported: spec.exported || 'unknown',
      calls: spec.calls || [],
      refs: spec.refs || [],
      source: spec.source,
      defaultExport: !!spec.defaultExport
    });
    return symbols.length - 1;
  }

  function nodeSpan(node) {
    return { startLine: lineOf(node.getStart(sf)), endLine: lineOf(node.getEnd()) };
  }

  function nodeSource(node) {
    return text.slice(node.getStart(sf), node.getEnd());
  }

  function memberName(node) {
    return node.name ? node.name.getText(sf) : '<anonymous>';
  }

  function heritageBases(node) {
    const bases = [];
    for (const clause of node.heritageClauses || []) {
      for (const t of clause.types) {
        const n = dottedName(t.expression);
        if (n && !bases.includes(n)) bases.push(n);
      }
    }
    return bases;
  }

  function isDefaultExport(node) {
    return hasModifier(node, K.DefaultKeyword) && hasModifier(node, K.ExportKeyword);
  }

  function addFunctionLike(node, kind, name, parent, signature, isTopExport) {
    const uses = finishUses(gather([node], false));
    const span = nodeSpan(node);
    return addSymbol({
      kind,
      name,
      parent,
      signature,
      params: paramsOf(node),
      returns: node.type ? collapse(node.type.getText(sf)) : '',
      decorators: decoratorsOf(node),
      doc: docOf(node),
      exported: exportedFlag(node, parent, name),
      calls: uses.calls,
      refs: uses.refs,
      source: nodeSource(node),
      defaultExport: isTopExport && isDefaultExport(node),
      ...span
    });
  }

  function visitClass(node, parent) {
    const name = node.name ? node.name.text : 'default';
    const uses = finishUses(gather([...(ts.canHaveDecorators(node) ? ts.getDecorators(node) || [] : []),
      ...(node.typeParameters || []), ...(node.heritageClauses || [])], false));
    const span = nodeSpan(node);
    const idx = addSymbol({
      kind: 'class',
      name,
      parent,
      signature: collapse(text.slice(node.getStart(sf), node.members.pos - 1)),
      decorators: decoratorsOf(node),
      bases: heritageBases(node),
      doc: docOf(node),
      exported: exportedFlag(node, parent, name),
      calls: uses.calls,
      refs: uses.refs,
      source: nodeSource(node),
      defaultExport: parent < 0 && (isDefaultExport(node) || defaultName === name),
      ...span
    });
    for (const member of node.members) {
      if (ts.isConstructorDeclaration(member)) {
        addFunctionLike(member, 'constructor', 'constructor', idx,
          headerText(member, member.body ? member.body.getStart(sf) : member.getEnd()), false);
      } else if (ts.isMethodDeclaration(member)) {
        addFunctionLike(member, 'method', memberName(member), idx,
          headerText(member, member.body ? member.body.getStart(sf) : member.getEnd()), false);
      } else if (ts.isGetAccessorDeclaration(member) || ts.isSetAccessorDeclaration(member)) {
        addFunctionLike(member, 'method', memberName(member), idx,
          headerText(member, member.body ? member.body.getStart(sf) : member.getEnd()), false);
      } else if (ts.isPropertyDeclaration(member)) {
        const init = member.initializer;
        if (init && (ts.isArrowFunction(init) || ts.isFunctionExpression(init))) {
          const uses2 = finishUses(gather([member], false));
          addSymbol({
            kind: 'method',
            name: memberName(member),
            parent: idx,
            signature: headerText(member, init.body.getStart(sf)),
            params: paramsOf(init),
            returns: init.type ? collapse(init.type.getText(sf)) : '',
            decorators: decoratorsOf(member),
            doc: docOf(member),
            calls: uses2.calls,
            refs: uses2.refs,
            source: nodeSource(member),
            ...nodeSpan(member)
          });
        } else {
          const uses2 = finishUses(gather([member], false));
          addSymbol({
            kind: 'property',
            name: memberName(member),
            parent: idx,
            signature: truncate(nodeSource(member).replace(/;\s*$/, ''), MAX_SIGNATURE_TEXT),
            returns: member.type ? collapse(member.type.getText(sf)) : '',
            decorators: decoratorsOf(member),
            doc: docOf(member),
            calls: uses2.calls,
            refs: uses2.refs,
            source: nodeSource(member),
            ...nodeSpan(member)
          });
        }
      }
    }
  }

  function visitInterface(node, parent) {
    const name = node.name.text;
    const uses = finishUses(gather([...(node.typeParameters || []), ...(node.heritageClauses || [])], false));
    const idx = addSymbol({
      kind: 'interface',
      name,
      parent,
      signature: collapse(text.slice(node.getStart(sf), node.members.pos - 1)),
      bases: heritageBases(node),
      doc: docOf(node),
      exported: exportedFlag(node, parent, name),
      calls: uses.calls,
      refs: uses.refs,
      source: nodeSource(node),
      ...nodeSpan(node)
    });
    for (const member of node.members) {
      if (ts.isMethodSignature(member)) {
        const u = finishUses(gather([member], false));
        addSymbol({
          kind: 'method',
          name: memberName(member),
          parent: idx,
          signature: truncate(nodeSource(member).replace(/[;,]\s*$/, ''), MAX_SIGNATURE_TEXT),
          params: paramsOf(member),
          returns: member.type ? collapse(member.type.getText(sf)) : '',
          doc: docOf(member),
          calls: u.calls,
          refs: u.refs,
          source: nodeSource(member),
          ...nodeSpan(member)
        });
      } else if (ts.isPropertySignature(member)) {
        const u = finishUses(gather([member], false));
        addSymbol({
          kind: 'property',
          name: memberName(member),
          parent: idx,
          signature: truncate(nodeSource(member).replace(/[;,]\s*$/, ''), MAX_SIGNATURE_TEXT),
          returns: member.type ? collapse(member.type.getText(sf)) : '',
          doc: docOf(member),
          calls: u.calls,
          refs: u.refs,
          source: nodeSource(member),
          ...nodeSpan(member)
        });
      }
    }
  }

  function requireSpecifier(init) {
    let expr = init;
    let member = null;
    if (expr && ts.isPropertyAccessExpression(expr)) {
      member = expr.name.text;
      expr = expr.expression;
    }
    if (expr && ts.isCallExpression(expr) && ts.isIdentifier(expr.expression) &&
        expr.expression.text === 'require' && expr.arguments.length === 1 &&
        ts.isStringLiteralLike(expr.arguments[0])) {
      return { module: expr.arguments[0].text, member };
    }
    return null;
  }

  function addRequireImport(stmt, decl, spec) {
    const bindings = [];
    if (ts.isIdentifier(decl.name)) {
      bindings.push(spec.member
        ? { local: decl.name.text, imported: spec.member, module: spec.module, depth: 1 }
        : { local: decl.name.text, imported: '*', module: spec.module, depth: 1 });
    } else if (ts.isObjectBindingPattern(decl.name)) {
      for (const el of decl.name.elements) {
        if (ts.isIdentifier(el.name)) {
          const imported = el.propertyName ? el.propertyName.getText(sf) : el.name.text;
          bindings.push({ local: el.name.text, imported, module: spec.module, depth: 1 });
        }
      }
    }
    imports.push({
      line: lineOf(stmt.getStart(sf)),
      endLine: lineOf(stmt.getEnd()),
      text: nodeSource(stmt),
      module: spec.module,
      star: false,
      bindings
    });
  }

  function visitVariableStatement(stmt, parent) {
    const flags = stmt.declarationList.flags;
    const isConst = (flags & ts.NodeFlags.Const) !== 0;
    const keyword = isConst ? 'const' : (flags & ts.NodeFlags.Let) !== 0 ? 'let' : 'var';
    const mods = ts.canHaveModifiers(stmt) ? ts.getModifiers(stmt) : undefined;
    const modText = mods ? mods.map((m) => m.getText(sf)).join(' ') + ' ' : '';
    const single = stmt.declarationList.declarations.length === 1;
    for (const decl of stmt.declarationList.declarations) {
      const req = requireSpecifier(decl.initializer);
      if (req && parent < 0) {
        addRequireImport(stmt, decl, req);
        continue;
      }
      const names = [];
      bindingNames(decl.name, names);
      const init = decl.initializer;
      const fnInit = init && (ts.isArrowFunction(init) || ts.isFunctionExpression(init)) ? init : null;
      const uses = finishUses(gather([decl], false));
      const node = single ? stmt : decl;
      const source = single ? nodeSource(stmt) : keyword + ' ' + nodeSource(decl);
      const span = single ? nodeSpan(stmt) : nodeSpan(decl);
      for (const name of names) {
        const nameIsDecl = ts.isIdentifier(decl.name);
        let signature;
        let kind;
        let params = [];
        let returns = '';
        if (fnInit && nameIsDecl) {
          kind = 'function';
          params = paramsOf(fnInit);
          returns = fnInit.type ? collapse(fnInit.type.getText(sf)) : '';
          const bodyStart = fnInit.body.getStart(sf);
          signature = collapse(modText + keyword + ' ' + text.slice(decl.getStart(sf), bodyStart));
        } else {
          kind = isConst ? 'constant' : 'variable';
          signature = modText + keyword + ' ' + name;
          if (nameIsDecl) {
            if (decl.type) signature += ': ' + collapse(decl.type.getText(sf));
            if (init) signature += ' = ' + truncate(init.getText(sf), MAX_VALUE_TEXT);
          }
          signature = collapse(signature);
          returns = decl.type && nameIsDecl ? collapse(decl.type.getText(sf)) : '';
        }
        declaredTop.add(name);
        addSymbol({
          kind,
          name,
          parent,
          signature,
          params,
          returns,
          doc: docOf(stmt),
          exported: exportedFlag(stmt, parent, name),
          calls: uses.calls,
          refs: uses.refs,
          source,
          defaultExport: parent < 0 && defaultName === name,
          ...span
        });
      }
    }
  }

  function visitStatements(statements, parent, scope) {
    for (const stmt of statements) {
      visitStatement(stmt, parent, scope);
    }
  }

  function visitStatement(stmt, parent, scope) {
    if (ts.isFunctionDeclaration(stmt)) {
      const name = stmt.name ? stmt.name.text : 'default';
      addFunctionLike(stmt, 'function', name, parent,
        headerText(stmt, stmt.body ? stmt.body.getStart(sf) : stmt.getEnd()), parent < 0);
      if (stmt.body) visitStatements(stmt.body.statements, symbols.length - 1, 'function');
      return;
    }
    if (ts.isClassDeclaration(stmt)) {
      visitClass(stmt, parent);
      return;
    }
    if (scope === 'function') {
      if (ts.isBlock(stmt)) visitStatements(stmt.statements, parent, scope);
      else if (ts.isIfStatement(stmt)) {
        visitStatement(stmt.thenStatement, parent, scope);
        if (stmt.elseStatement) visitStatement(stmt.elseStatement, parent, scope);
      } else if (ts.isTryStatement(stmt)) {
        visitStatements(stmt.tryBlock.statements, parent, scope);
        if (stmt.catchClause) visitStatements(stmt.catchClause.block.statements, parent, scope);
        if (stmt.finallyBlock) visitStatements(stmt.finallyBlock.statements, parent, scope);
      }
      return;
    }
    if (ts.isInterfaceDeclaration(stmt)) {
      visitInterface(stmt, parent);
      return;
    }
    if (ts.isTypeAliasDeclaration(stmt)) {
      const name = stmt.name.text;
      const uses = finishUses(gather([stmt], false));
      addSymbol({
        kind: 'type',
        name,
        parent,
        signature: truncate(nodeSource(stmt).replace(/;\s*$/, ''), MAX_SIGNATURE_TEXT),
        doc: docOf(stmt),
        exported: exportedFlag(stmt, parent, name),
        calls: uses.calls,
        refs: uses.refs,
        source: nodeSource(stmt),
        ...nodeSpan(stmt)
      });
      return;
    }
    if (ts.isEnumDeclaration(stmt)) {
      const name = stmt.name.text;
      const uses = finishUses(gather([stmt], false));
      const members = stmt.members.map((m) => m.name.getText(sf)).join(', ');
      const mods = ts.canHaveModifiers(stmt) ? ts.getModifiers(stmt) : undefined;
      const modText = mods ? mods.map((m) => m.getText(sf)).join(' ') + ' ' : '';
      addSymbol({
        kind: 'enum',
        name,
        parent,
        signature: truncate(modText + 'enum ' + name + ' { ' + members + ' }', MAX_SIGNATURE_TEXT),
        doc: docOf(stmt),
        exported: exportedFlag(stmt, parent, name),
        calls: uses.calls,
        refs: uses.refs,
        source: nodeSource(stmt),
        ...nodeSpan(stmt)
      });
      return;
    }
    if (ts.isModuleDeclaration(stmt)) {
      const name = stmt.name.getText(sf);
      const idx = addSymbol({
        kind: 'namespace',
        name,
        parent,
        signature: headerText(stmt, stmt.body ? stmt.body.getStart(sf) : stmt.getEnd()),
        doc: docOf(stmt),
        exported: exportedFlag(stmt, parent, name),
        source: nodeSource(stmt),
        ...nodeSpan(stmt)
      });
      let body = stmt.body;
      while (body && ts.isModuleDeclaration(body)) body = body.body;
      if (body && ts.isModuleBlock(body)) visitStatements(body.statements, idx, 'namespace');
      return;
    }
    if (ts.isVariableStatement(stmt)) {
      visitVariableStatement(stmt, parent);
      return;
    }
    if (ts.isImportDeclaration(stmt)) {
      addImport(stmt);
      return;
    }
    if (ts.isImportEqualsDeclaration(stmt)) {
      const ref = stmt.moduleReference;
      if (ts.isExternalModuleReference(ref) && ts.isStringLiteralLike(ref.expression)) {
        imports.push({
          line: lineOf(stmt.getStart(sf)),
          endLine: lineOf(stmt.getEnd()),
          text: nodeSource(stmt),
          module: ref.expression.text,
          star: false,
          bindings: [{ local: stmt.name.text, imported: '*', module: ref.expression.text, depth: 1 }]
        });
      }
      return;
    }
    if (ts.isExpressionStatement(stmt)) {
      visitCommonJsAssignment(stmt, parent);
    }
  }

  function addImport(stmt) {
    const module = stmt.moduleSpecifier.text;
    const bindings = [];
    const clause = stmt.importClause;
    if (clause) {
      if (clause.name) bindings.push({ local: clause.name.text, imported: 'default', module, depth: 1 });
      const nb = clause.namedBindings;
      if (nb && ts.isNamespaceImport(nb)) {
        bindings.push({ local: nb.name.text, imported: '*', module, depth: 1 });
      } else if (nb && ts.isNamedImports(nb)) {
        for (const el of nb.elements) {
          bindings.push({
            local: el.name.text,
            imported: (el.propertyName || el.name).text,
            module,
            depth: 1
          });
        }
      }
    }
    imports.push({
      line: lineOf(stmt.getStart(sf)),
      endLine: lineOf(stmt.getEnd()),
      text: nodeSource(stmt),
      module,
      star: false,
      bindings
    });
  }

  function exportsTarget(left) {
    if (ts.isPropertyAccessExpression(left)) {
      const base = dottedName(left.expression);
      if (base === 'exports' || base === 'module.exports') return { name: left.name.text };
      if (base === 'module' && left.name.text === 'exports') return { whole: true };
    }
    return null;
  }

  function visitCommonJsAssignment(stmt, parent) {
    const e = stmt.expression;
    if (!ts.isBinaryExpression(e) || e.operatorToken.kind !== K.EqualsToken || parent >= 0) return;
    const target = exportsTarget(e.left);
    if (!target) return;
    const right = e.right;
    if (target.name && (ts.isArrowFunction(right) || ts.isFunctionExpression(right))) {
      const uses = finishUses(gather([right], false));
      declaredTop.add(target.name);
      addSymbol({
        kind: 'function',
        name: target.name,
        parent,
        signature: collapse(text.slice(stmt.getStart(sf), right.body.getStart(sf))),
        params: paramsOf(right),
        returns: right.type ? collapse(right.type.getText(sf)) : '',
        doc: docOf(stmt),
        exported: 'yes',
        calls: uses.calls,
        refs: uses.refs,
        source: nodeSource(stmt),
        ...nodeSpan(stmt)
      });
    }
  }

  function collectExports(statements) {
    for (const stmt of statements) {
      if (ts.isExportDeclaration(stmt)) {
        if (stmt.exportClause && ts.isNamedExports(stmt.exportClause)) {
          for (const el of stmt.exportClause.elements) {
            exportNames.push(el.name.text);
            if (!stmt.moduleSpecifier) exportedLocal.add((el.propertyName || el.name).text);
          }
        }
      } else if (ts.isExportAssignment(stmt)) {
        if (ts.isIdentifier(stmt.expression)) {
          exportedLocal.add(stmt.expression.text);
          if (!stmt.isExportEquals) defaultName = stmt.expression.text;
          exportNames.push(stmt.isExportEquals ? 'export=' : 'default');
        } else {
          exportNames.push(stmt.isExportEquals ? 'export=' : 'default');
        }
      } else if (ts.isExpressionStatement(stmt) && ts.isBinaryExpression(stmt.expression) &&
          stmt.expression.operatorToken.kind === K.EqualsToken) {
        const target = exportsTarget(stmt.expression.left);
        if (!target) continue;
        const right = stmt.expression.right;
        if (target.name) {
          exportNames.push(target.name);
          if (ts.isIdentifier(right)) exportedLocal.add(right.text);
        } else if (target.whole) {
          exportNames.push('module.exports');
          if (ts.isIdentifier(right)) exportedLocal.add(right.text);
          if (ts.isObjectLiteralExpression(right)) {
            for (const prop of right.properties) {
              if (ts.isShorthandPropertyAssignment(prop)) {
                exportedLocal.add(prop.name.text);
                exportNames.push(prop.name.text);
              } else if (ts.isPropertyAssignment(prop) && ts.isIdentifier(prop.initializer)) {
                exportedLocal.add(prop.initializer.text);
                exportNames.push(prop.name.getText(sf));
              } else if (prop.name) {
                exportNames.push(prop.name.getText(sf));
              }
            }
          }
        }
      } else if (hasModifier(stmt, K.ExportKeyword)) {
        if (hasModifier(stmt, K.DefaultKeyword)) {
          exportNames.push('default');
        } else if (ts.isVariableStatement(stmt)) {
          const names = [];
          for (const d of stmt.declarationList.declarations) bindingNames(d.name, names);
          for (const n of names) exportNames.push(n);
        } else if (stmt.name && ts.isIdentifier(stmt.name)) {
          exportNames.push(stmt.name.text);
        }
      }
    }
  }

  collectExports(sf.statements);
  visitStatements(sf.statements, -1, 'file');

  for (const s of symbols) {
    if (s.parent < 0) declaredTop.add(s.name);
  }
  for (const imp of imports) {
    for (const b of imp.bindings) declaredTop.add(b.local);
  }

  const typeParamNames = new Set();
  (function collectTypeParams(node) {
    if (node.kind === K.TypeParameter) typeParamNames.add(node.name.text);
    ts.forEachChild(node, collectTypeParams);
  })(sf);

  function filterNames(names) {
    return names.filter((n) => {
      const root = n.split('.')[0];
      if (root === 'this' || root === 'super') return true;
      if (typeParamNames.has(root) && !declaredTop.has(root)) return false;
      if (globalNames.has(root) && !declaredTop.has(root)) return false;
      return true;
    });
  }

  for (const s of symbols) {
    s.calls = filterNames(s.calls);
    s.refs = filterNames(s.refs).filter((r) => r !== 'this' && r !== 'super');
  }

  return {
    ok: true,
    language,
    imports,
    symbols,
    exports: exportNames.filter((n, i) => exportNames.indexOf(n) === i)
  };
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const ts = loadTypescript();
  if (!ts) {
    const message = incompatibleFound
      ? 'the installed "typescript" package does not expose the JavaScript compiler API (TypeScript 7 native builds do not); install TypeScript 5 or 6 (npm install typescript@6) or point MFDP_TYPESCRIPT at one'
      : 'the "typescript" npm package was not found; install it (npm install -g typescript@6) or set MFDP_TYPESCRIPT';
    emit(failure('adapter_unavailable', message));
    return;
  }
  let data;
  try {
    data = fs.readFileSync(0);
  } catch (e) {
    data = Buffer.alloc(0);
  }
  let text;
  try {
    text = new TextDecoder('utf-8', { fatal: true }).decode(data);
  } catch (e) {
    emit(failure('parse_error', 'source is not valid UTF-8'));
    return;
  }
  if (text.charCodeAt(0) === 0xfeff) text = text.slice(1);
  try {
    emit(analyze(ts, text, args.language, args.filename));
  } catch (e) {
    emit(failure('parse_error', 'analysis failed: ' + (e && e.message ? e.message : String(e))));
  }
}

main();
