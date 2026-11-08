import { createMachine, type StateNode } from './fsm';

export type ProjectsOp = 'deleting' | 'restoring' | 'retrying';
type ProjectsOpsEvent = { type: 'BEGIN'; op: ProjectsOp; id?: string } | { type: 'END'; op: ProjectsOp };
interface ProjectsOpsContext { id: string | null }
type Node = StateNode<'idle' | ProjectsOp, ProjectsOpsEvent, ProjectsOpsContext>;

const op = (name: ProjectsOp): Node => ({ on: { END: { target: 'idle', guard: (_, e) => e.op === name, action: () => ({ id: null }) } } });
export const projectsOps = createMachine<'idle' | ProjectsOp, ProjectsOpsEvent, ProjectsOpsContext>({
  id: 'projects-ops', initial: 'idle', strict: true, context: { id: null },
  states: {
    idle: { on: { BEGIN: (['deleting', 'restoring', 'retrying'] as const).map((target) => ({ target, guard: (_: ProjectsOpsContext, e: { op: ProjectsOp }) => e.op === target,
      action: (_: ProjectsOpsContext, e: { id?: string }) => ({ id: e.id ?? null }) })) } },
    deleting: op('deleting'), restoring: op('restoring'), retrying: op('retrying'),
  },
});

export async function runProjectsOp(op: ProjectsOp, task: () => Promise<void>, id?: string): Promise<boolean> {
  if (!projectsOps.can({ type: 'BEGIN', op, id })) return false;
  projectsOps.send({ type: 'BEGIN', op, id });
  try { await task(); } finally { projectsOps.send({ type: 'END', op }); }
  return true;
}
