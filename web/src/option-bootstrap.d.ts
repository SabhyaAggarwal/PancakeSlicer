declare module 'virtual:option-bootstrap' {
  export const quick: Record<string, { type: string; mode: string; enum_values: unknown[] }>;
  export const vectors: Record<string, string>;
  export const removed: string[];
}
