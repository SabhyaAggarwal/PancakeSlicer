import type { Ev, MachineDef, Transition } from './fsm';

export function machineDiagram<S extends string, E extends Ev, C>(def: MachineDef<S, E, C>): string {
  const lines = ['stateDiagram-v2', `  [*] --> ${def.initial}`];
  for (const [state, node] of Object.entries(def.states) as [S, MachineDef<S, E, C>['states'][S]][]) {
    const edges = new Map<string, string[]>();
    const edge = (value: S | Transition<S, C, E>, label: string, index?: number) => {
      const t = typeof value === 'string' ? { target: value } : value;
      const to = t.target ?? state;
      const labels = edges.get(to) ?? [];
      labels.push(`${label}${t.guard ? ` [guard ${index ?? 1}]` : ''}${t.target === undefined ? ' (internal)' : ''}`);
      edges.set(to, labels);
    };
    for (const [event, value] of Object.entries(node.on ?? {})) {
      const choices = (Array.isArray(value) ? value : [value]) as (S | Transition<S, C, E>)[];
      choices.forEach((t, i) => edge(t, event, i + 1));
    }
    if (node.after) edge(node.after, `${typeof node.after.ms === 'number' ? node.after.ms : 'context'} ms`);
    for (const [to, labels] of edges) lines.push(`  ${state} --> ${to}: ${labels.join(' / ')}`);
  }
  return lines.join('\n');
}
