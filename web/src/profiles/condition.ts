import type { Config, ConfigValue } from '../api/types';
import { OPTIONS } from '../options';

type Value = string | number | boolean | null | ConfigValue[] | { pattern: string };
type Node = { kind: 'value'; value: Value } | { kind: 'variable'; name: string; index?: Node }
  | { kind: 'unary'; op: string; value: Node } | { kind: 'binary'; op: string; left: Node; right: Node }
  | { kind: 'ternary'; when: Node; yes: Node; no: Node } | { kind: 'call'; name: string; args: Node[] };
interface Token { text: string; value?: Value }
export type ConditionResult = { ok: true; value: boolean } | { ok: false; error: string };
const compiled = new Map<string, Node>();
const operators: Record<string, number> = { or: 1, '||': 1, and: 2, '&&': 2, '==': 3, '!=': 3, '<>': 3, '=~': 3, '!~': 3,
  '<': 4, '>': 4, '<=': 4, '>=': 4, '+': 5, '-': 5, '*': 6, '/': 6, '%': 6 };
const fail = (message: string): never => { throw new Error(message); };
function tokenize(expression: string): Token[] {
  if (expression.length > 4096) fail('Compatibility expression is too long');
  const tokens: Token[] = []; let position = 0;
  while (position < expression.length) {
    const rest = expression.slice(position), space = /^\s+/.exec(rest);
    if (space) { position += space[0].length; continue; }
    if (rest[0] === '"') {
      const quoted = /^"(?:[^"\\]|\\.)*"/.exec(rest)?.[0];
      if (!quoted) return fail('Unclosed compatibility string');
      tokens.push({text: 'value', value: JSON.parse(quoted)}); position += quoted.length; continue;
    }
    if (rest[0] === '/' && ['=~', '!~', ','].includes(tokens.at(-1)?.text ?? '')) {
      let end = 1;
      for (; end < rest.length; end++) if (rest[end] === '\\') end++; else if (rest[end] === '/') break;
      if (end === rest.length) fail('Unclosed compatibility pattern');
      const pattern = rest.slice(1, end);
      if (/\(\?|\\[AzGQEpPkK]|\[\[:/.test(pattern)) fail('Unsupported native compatibility pattern');
      tokens.push({text: 'value', value: {pattern}}); position += end + 1; continue;
    }
    const number = /^(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?/.exec(rest)?.[0];
    if (number) { tokens.push({text: 'value', value: Number(number)}); position += number.length; continue; }
    const token = /^(?:&&|\|\||==|!=|<>|>=|<=|=~|!~|[A-Za-z_]\w*|[()[\]!,?:<>+*/%=-])/.exec(rest)?.[0];
    if (!token) return fail('Unsupported compatibility syntax');
    tokens.push(token === 'true' || token === 'false' ? {text: 'value', value: token === 'true'} : {text: token}); position += token.length;
  }
  return tokens;
}
function compile(expression: string): Node {
  const ready = compiled.get(expression); if (ready) return ready;
  const tokens = tokenize(expression); let pos = 0;
  const expect = (token: string) => { if (tokens[pos++]?.text !== token) fail(`Expected ${token}`); };
  function parse(min = 0): Node {
    const token = tokens[pos++]; if (!token) fail('Incomplete compatibility expression');
    let left: Node;
    if (token.text === '(') { left = parse(); expect(')'); }
    else if (['!', 'not', '-', '+'].includes(token.text)) left = {kind: 'unary', op: token.text, value: parse(7)};
    else if (token.text === 'value') left = {kind: 'value', value: token.value!};
    else if (/^[A-Za-z_]\w*$/.test(token.text)) {
      if (tokens[pos]?.text === '(') {
        pos++; const args: Node[] = [];
        if (tokens[pos]?.text !== ')') do { args.push(parse()); } while (tokens[pos]?.text === ',' && ++pos);
        expect(')'); left = {kind: 'call', name: token.text, args};
      } else {
        let index: Node | undefined;
        if (tokens[pos]?.text === '[') { pos++; index = parse(); expect(']'); }
        left = {kind: 'variable', name: token.text, index};
      }
    } else fail('Invalid compatibility expression');
    while (pos < tokens.length && (operators[tokens[pos].text] ?? -1) >= min) {
      const op = tokens[pos++].text, right = parse(operators[op] + 1); left = {kind: 'binary', op, left: left!, right};
    }
    if (!min && tokens[pos]?.text === '?') { pos++; const yes = parse(); expect(':'); left = {kind: 'ternary', when: left!, yes, no: parse()}; }
    return left!;
  }
  const root = parse(); if (pos !== tokens.length) fail('Trailing compatibility syntax');
  if (compiled.size >= 512) compiled.delete(compiled.keys().next().value!);
  compiled.set(expression, root); return root;
}
const boolean = (value: Value) => typeof value === 'boolean' ? value : fail('Compatibility predicate must be boolean');
const numeric = (value: Value) => typeof value === 'number' && Number.isFinite(value) ? value : fail('Compatibility value must be numeric');
const text = (value: Value): string => typeof value === 'boolean' ? value ? 'true' : 'false' : typeof value === 'string' || typeof value === 'number' ? String(value) : fail('Compatibility value must be scalar');
function evaluate(node: Node, config: Config, wholeVector = false): Value {
  const run = (n: Node) => evaluate(n, config);
  if (node.kind === 'value') return node.value;
  if (node.kind === 'variable') {
    let value = config[node.name];
    if (value === undefined) return fail(`Missing compatibility value: ${node.name}`);
    if (wholeVector && Array.isArray(value) && !node.index) return value;
    if (Array.isArray(value)) {
      const index = node.index ? numeric(run(node.index)) : 0;
      if (!Number.isInteger(index) || index < 0 || index >= value.length) fail(`Invalid compatibility index: ${node.name}`);
      value = value[index];
    } else if (node.index) fail(`Compatibility value is not a vector: ${node.name}`);
    const type = OPTIONS[node.name]?.type ?? '';
    if (value === 'nil') return null;
    if (type === 'bool' || type === 'bools') return value === '1' || value === 1 || value === true || value === 'true';
    if (/^(float|int|percent)/.test(type)) return numeric(Number(value));
    return value;
  }
  if (node.kind === 'ternary') return run(boolean(run(node.when)) ? node.yes : node.no);
  if (node.kind === 'unary') {
    const value = run(node.value);
    return node.op === '!' || node.op === 'not' ? !boolean(value) : node.op === '-' ? -numeric(value) : numeric(value);
  }
  if (node.kind === 'call') {
    if (['empty', 'size', 'is_nil'].includes(node.name)) {
      if (node.args.length !== 1) fail('Invalid compatibility function arguments');
      const value = evaluate(node.args[0], config, node.name !== 'is_nil');
      if (node.name === 'is_nil') return value === null;
      if (!Array.isArray(value)) return fail('Compatibility function requires a vector');
      return node.name === 'empty' ? value.length === 0 : value.length;
    }
    const args = node.args.map(run);
    if (node.name === 'one_of' && args.length) return args.slice(1).some(value => typeof value === 'object' && value && 'pattern' in value
      ? new RegExp(`^(?:${value.pattern})$`).test(text(args[0])) : compare(args[0], value) === 0);
    if ((node.name === 'min' || node.name === 'max') && args.length === 2) return Math[node.name](...args.map(numeric));
    if (args.length === 1 && ['int', 'round', 'ceil', 'floor'].includes(node.name)) {
      const value = numeric(args[0]); return node.name === 'int' ? Math.trunc(value) : node.name === 'round' ? Math.sign(value) * Math.round(Math.abs(value)) : Math[node.name as 'ceil' | 'floor'](value);
    }
    return fail(`Unsupported compatibility function: ${node.name}`);
  }
  const left = run(node.left), right = run(node.right), op = node.op;
  if (op === '&&' || op === 'and') return boolean(left) && boolean(right);
  if (op === '||' || op === 'or') return boolean(left) || boolean(right);
  if (op === '=~' || op === '!~') {
    if (typeof left !== 'string' || !right || typeof right !== 'object' || !('pattern' in right)) fail('Invalid compatibility regex operands');
    const match = new RegExp(`^(?:${(right as {pattern:string}).pattern})$`).test(left as string); return op === '=~' ? match : !match;
  }
  if (['==', '!=', '<>', '<', '>', '<=', '>='].includes(op)) {
    const cmp = compare(left, right); return op === '==' ? cmp === 0 : op === '!=' || op === '<>' ? cmp !== 0 : op === '<' ? cmp < 0 : op === '>' ? cmp > 0 : op === '<=' ? cmp <= 0 : cmp >= 0;
  }
  if (op === '+' && (typeof left === 'string' || typeof right === 'string')) return text(left) + text(right);
  const a = numeric(left), b = numeric(right);
  if ((op === '/' || op === '%') && b === 0) fail('Compatibility division by zero');
  return op === '+' ? a + b : op === '-' ? a - b : op === '*' ? a * b : op === '%' ? a % b : a / b;
}
function compare(a: Value, b: Value): number {
  if (typeof a === 'number' && typeof b === 'number') return Math.abs(a - b) < 1e-8 ? 0 : a < b ? -1 : 1;
  if (typeof a === 'string' || typeof b === 'string') { const x = text(a), y = text(b); return x === y ? 0 : x < y ? -1 : 1; }
  if (typeof a === 'boolean' && typeof b === 'boolean') return a === b ? 0 : a ? 1 : -1;
  return fail('Cannot compare compatibility types');
}
export function evaluateProfileCondition(expression: string, config: Config): ConditionResult {
  try { return {ok: true, value: boolean(evaluate(compile(expression), config))}; }
  catch (error) { return {ok: false, error: error instanceof Error ? error.message : String(error)}; }
}
