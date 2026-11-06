


import type { IncomingMessage, OutgoingHttpHeaders, ServerResponse } from 'node:http';
import { createGzip, type Gzip } from 'node:zlib';
import type { Plugin } from 'vite';

const COMPRESSIBLE = /javascript|css|json|html|svg|xml|text\/plain/;

function compress(req: IncomingMessage, res: ServerResponse, next: () => void) {
  if (req.method === 'HEAD' || req.url?.startsWith('/api/') || !/\bgzip\b/.test(String(req.headers['accept-encoding'] ?? ''))) return next();
  const write = res.write.bind(res) as (chunk: unknown, ...rest: unknown[]) => boolean;
  const end = res.end.bind(res) as (...args: unknown[]) => ServerResponse;
  const writeHead = res.writeHead.bind(res) as (status: number, ...rest: unknown[]) => ServerResponse;
  let gzip: Gzip | null = null;
  let decided = false;
  const decide = () => {
    if (decided) return;
    decided = true;
    const type = String(res.getHeader('Content-Type') ?? '');
    if (res.statusCode < 200 || res.statusCode === 204 || res.statusCode === 304 || res.getHeader('Content-Encoding') ||
      !COMPRESSIBLE.test(type) || Number(res.getHeader('Content-Length') ?? Infinity) < 1024) return;
    res.removeHeader('Content-Length');
    res.setHeader('Content-Encoding', 'gzip');
    res.setHeader('Vary', 'Accept-Encoding');
    gzip = createGzip({ level: 1 });
    gzip.on('data', (chunk: Buffer) => write(chunk));
    gzip.on('end', () => end());

    gzip.on('drain', () => res.emit('drain'));
  };

  res.writeHead = ((status: number, ...rest: unknown[]) => {
    const headers = rest.find((r) => r && typeof r === 'object') as OutgoingHttpHeaders | undefined;
    for (const [key, value] of Object.entries(headers ?? {})) if (value !== undefined) res.setHeader(key, value);
    res.statusCode = status;
    decide();
    return writeHead(status);
  }) as typeof res.writeHead;
  res.write = ((chunk: unknown, ...rest: unknown[]) => {
    decide();
    return gzip ? gzip.write(chunk as Buffer, ...(rest.filter((r) => typeof r === 'string') as [BufferEncoding])) : write(chunk, ...rest);
  }) as typeof res.write;
  res.end = ((...args: unknown[]) => {
    decide();
    if (!gzip) return end(...args);
    const [chunk, encoding] = args;
    if (chunk !== undefined && typeof chunk !== 'function') gzip.end(chunk as Buffer, (typeof encoding === 'string' ? encoding : undefined) as BufferEncoding);
    else gzip.end();
    return res;
  }) as typeof res.end;
  next();
}

export function devCompression(): Plugin {
  return { name: 'orca-dev-compression', apply: 'serve', configureServer(server) { server.middlewares.use(compress); } };
}
