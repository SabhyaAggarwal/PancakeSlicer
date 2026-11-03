export type WireRequest =
  | { type: 'capture'; id: number; value: unknown }
  | { type: 'project'; id: number; capture: number; body: { operationId: string; expectedRevision: number; record: Record<string, unknown> } }
  | { type: 'draft'; id: number; capture: number; operationId: string }
  | { type: 'parse'; id: number; text: string }
  | { type: 'release'; capture: number };
export type WireResponse =
  | { type: 'digest'; id: number; digest: string }
  | { type: 'text'; id: number; text: string }
  | { type: 'parsed'; id: number; value: unknown }
  | { type: 'error'; id: number; name: string; message: string };
