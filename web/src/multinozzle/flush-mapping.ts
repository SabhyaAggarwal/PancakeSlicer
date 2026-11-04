export function retainFlushSlots(matrix: number[] | null | undefined, count: number, kept: number[]): number[] | null {
  if (!matrix || count < 1 || matrix.length % (count * count)) return null;
  return Array.from({ length: matrix.length / (count * count) }, (_, nozzle) =>
    kept.flatMap((from) => kept.map((to) => matrix[nozzle * count * count + from * count + to]))).flat();
}
