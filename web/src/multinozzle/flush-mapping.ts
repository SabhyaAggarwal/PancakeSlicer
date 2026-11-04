export function retainFlushSlots(matrix: number[] | null | undefined, count: number, kept: number[]): number[] | null {
  if (!matrix || count < 1 || matrix.length % (count * count)) return null;
  return Array.from({ length: matrix.length / (count * count) }, (_, nozzle) =>
    kept.flatMap((from) => kept.map((to) => matrix[nozzle * count * count + from * count + to]))).flat();
}

export function insertFlushSlots(matrix: number[], count: number, previous: number[], calculated: number[]): number[] {
  const size = previous.length, area = count * count;
  return calculated.map((value, index) => {
    const from = previous[Math.floor(index / size) % size], to = previous[index % size];
    const nozzle = matrix.length === area ? 0 : Math.floor(index / (size * size));
    return from < 0 || to < 0 ? value : matrix[nozzle * area + from * count + to];
  });
}
