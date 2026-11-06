import { HighlightStyle, StreamLanguage, syntaxHighlighting, type StringStream } from '@codemirror/language';
import { Tag, tags } from '@lezer/highlight';





const unknown = Tag.define();
const KEYWORDS = new Set(['if', 'elsif', 'else', 'endif', 'and', 'or', 'not', 'local', 'global', 'true', 'false']);
const FUNCTIONS = new Set(['min', 'max', 'int', 'round', 'digits', 'zdigits', 'is_nil', 'one_of', 'interpolate_table', 'regex_replace', 'size', 'empty', 'filament_change', 'random', 'repeat']);

interface State { expr: number; locals: Set<string>; declare: boolean; lineStart: boolean }

export function gcodeLanguage(known: ReadonlySet<string>) {
  const isKnown = (name: string, s: State) => known.has(name) || s.locals.has(name);
  return StreamLanguage.define<State>({
    name: 'orca-gcode',
    startState: () => ({ expr: 0, locals: new Set(), declare: false, lineStart: true }),
    copyState: (s) => ({ ...s, locals: new Set(s.locals) }),
    token(stream: StringStream, s: State) {
      if (stream.sol()) s.lineStart = true;
      if (stream.eatSpace()) return null;
      if (s.expr > 0) return expression(stream, s, isKnown);
      if (stream.peek() === ';') { stream.skipToEnd(); return 'comment'; }
      if (stream.eat('{')) { s.expr = 1; s.lineStart = false; return 'brace'; }
      if (stream.match(/^\[([A-Za-z_]\w*)\]/, false)) {
        const m = stream.match(/^\[([A-Za-z_]\w*)\]/) as RegExpMatchArray;
        const name = m[1].replace(/_\d+$/, '');
        return isKnown(m[1], s) || isKnown(name, s) ? 'placeholder' : 'unknown';
      }
      const first = s.lineStart; s.lineStart = false;
      if (first && stream.match(/^[GMTgmt]\d+(?:\.\d+)?/)) return 'command';
      if (stream.match(/^[A-Za-z](?=[-+.\d{[])/)) return 'param';
      if (stream.match(/^[-+]?(?:\d+\.?\d*|\.\d+)/)) return 'number';
      stream.next();
      return null;
    },
    languageData: { commentTokens: { line: ';' } },
    tokenTable: { command: tags.keyword, param: tags.propertyName, placeholder: tags.variableName, unknown, brace: tags.brace, control: tags.controlKeyword, fn: tags.function(tags.variableName) },
  });
}

function expression(stream: StringStream, s: State, isKnown: (name: string, s: State) => boolean): string | null {
  if (stream.eat('}')) { s.expr = 0; s.declare = false; return 'brace'; }
  if (stream.eat('{')) { s.expr++; return 'brace'; }
  if (stream.match(/^"(?:[^"\\]|\\.)*"?/)) return 'string';
  if (stream.match(/^(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?/i)) return 'number';
  const word = stream.match(/^[A-Za-z_]\w*/) as RegExpMatchArray | null;
  if (word) {
    const name = word[0];
    if (KEYWORDS.has(name)) { s.declare = name === 'local' || name === 'global'; return name === 'true' || name === 'false' ? 'atom' : 'control'; }
    if (s.declare) { s.locals.add(name); s.declare = false; return 'placeholder'; }
    if (FUNCTIONS.has(name) && stream.match(/^\s*\(/, false)) return 'fn';
    return isKnown(name, s) ? 'placeholder' : 'unknown';
  }
  if (stream.match(/^(?:==|!=|<>|<=|>=|&&|\|\||[-+*/%<>=!?:,()[\]])/)) return 'operator';
  stream.next();
  return null;
}


export const gcodeHighlight = syntaxHighlighting(HighlightStyle.define([
  { tag: tags.keyword, color: 'var(--gc-command)', fontWeight: '600' },
  { tag: tags.propertyName, color: 'var(--gc-param)' },
  { tag: tags.number, color: 'var(--gc-number)' },
  { tag: tags.comment, color: 'var(--gc-comment)', fontStyle: 'italic' },
  { tag: tags.variableName, color: 'var(--gc-placeholder)' },
  { tag: unknown, color: 'var(--gc-unknown)', textDecoration: 'underline wavy', textDecorationThickness: '1px' },
  { tag: [tags.controlKeyword, tags.function(tags.variableName)], color: 'var(--gc-control)', fontWeight: '600' },
  { tag: [tags.brace, tags.operator], color: 'var(--gc-operator)' },
  { tag: [tags.string, tags.atom], color: 'var(--gc-string)' },
]));
