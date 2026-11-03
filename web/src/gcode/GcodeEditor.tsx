import { useEffect, useMemo, useRef } from 'react';
import { autocompletion, closeBrackets, closeBracketsKeymap, completionKeymap } from '@codemirror/autocomplete';
import { defaultKeymap, history, historyKeymap } from '@codemirror/commands';
import { bracketMatching } from '@codemirror/language';
import { unifiedMergeView } from '@codemirror/merge';
import { highlightSelectionMatches, openSearchPanel, search, searchKeymap } from '@codemirror/search';
import { Compartment, EditorState } from '@codemirror/state';
import { drawSelection, EditorView, highlightActiveLine, highlightActiveLineGutter, keymap, lineNumbers } from '@codemirror/view';
import { gcodeHighlight, gcodeLanguage } from './language';
import { placeholderCompletion, placeholdersFor } from './placeholders';

export function GcodeEditor({ optionKey, label, value, template, diff, readOnly, configKeys, onCommit, onView }: {
  optionKey: string; label: string; value: string; template: string; diff: boolean; readOnly?: boolean;
  configKeys: readonly string[];
  onCommit(text: string): void; onView?(view: EditorView | null): void;
}) {
  const host = useRef<HTMLDivElement>(null), view = useRef<EditorView | null>(null);
  const commit = useRef(onCommit), latest = useRef(value); commit.current = onCommit; latest.current = value;
  const merge = useMemo(() => new Compartment(), []), editable = useMemo(() => new Compartment(), []);
  const placeholders = useMemo(() => placeholdersFor(optionKey), [optionKey]);

  useEffect(() => {
    const known = new Set([...placeholders.keys(), ...configKeys]);
    let idle: ReturnType<typeof setTimeout> | undefined;
    const flush = (ed: EditorView) => { clearTimeout(idle); const text = ed.state.doc.toString(); if (text !== latest.current) commit.current(text); };
    const v = new EditorView({
      parent: host.current!,
      state: EditorState.create({
        doc: value,
        extensions: [
          lineNumbers(), highlightActiveLineGutter(), highlightActiveLine(), drawSelection(), history(), bracketMatching(), closeBrackets(),
          search({ top: true }), highlightSelectionMatches(), gcodeLanguage(known), gcodeHighlight,
          autocompletion({ override: [placeholderCompletion(placeholders)], icons: false }),
          keymap.of([...closeBracketsKeymap, ...defaultKeymap, ...searchKeymap, ...historyKeymap, ...completionKeymap]),
          EditorView.lineWrapping,
          EditorView.contentAttributes.of({ 'aria-label': label, spellcheck: 'false' }),
          EditorView.domEventHandlers({ blur: (_e, ed) => flush(ed) }),
          EditorView.updateListener.of((u) => { if (u.docChanged) { clearTimeout(idle); idle = setTimeout(() => flush(u.view), 300); } }),
          editable.of(readOnly ? [EditorState.readOnly.of(true), EditorView.editable.of(false)] : []),
          merge.of(diff ? unifiedMergeView({ original: template, mergeControls: false, gutter: true }) : []),
        ],
      }),
    });
    view.current = v; onView?.(v);
    return () => { flush(v); onView?.(null); v.destroy(); view.current = null; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [optionKey, placeholders]);

  useEffect(() => {
    const v = view.current;
    if (!v || v.hasFocus || v.state.doc.toString() === value) return;
    v.dispatch({ changes: { from: 0, to: v.state.doc.length, insert: value } });
  }, [value]);
  useEffect(() => { view.current?.dispatch({ effects: merge.reconfigure(diff ? unifiedMergeView({ original: template, mergeControls: false, gutter: true }) : []) }); }, [diff, template, merge]);
  useEffect(() => { view.current?.dispatch({ effects: editable.reconfigure(readOnly ? [EditorState.readOnly.of(true), EditorView.editable.of(false)] : []) }); }, [readOnly, editable]);

  return <div ref={host} className="gcode-editor" />;
}

export { openSearchPanel };
