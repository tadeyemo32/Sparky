/**
 * Vercel Serverless Proxy — /api/*
 *
 * Proxies all /api/* browser requests server-side to the backend.
 * BACKEND_URL and SPARKY_KEY are read from Vercel env vars — neither
 * ever appears in the browser bundle or network traffic.
 *
 * Uses only raw Node.js http/https APIs so there is no dependency on
 * @vercel/node helpers at runtime. TLS verification is intentionally
 * disabled (self-signed backend cert; we own both ends).
 */
import type { IncomingMessage, ServerResponse } from 'node:http';
import * as https from 'node:https';
import * as http from 'node:http';

export const config = { api: { bodyParser: false } };

const BACKEND      = (process.env.BACKEND_URL ?? '').replace(/\/$/, '');
const KEY          = process.env.SPARKY_KEY ?? '';
const ORIGIN       = process.env.SPARKY_ALLOWED_ORIGIN ?? 'https://sparky-tau.vercel.app';
const PROXY_SECRET = process.env.SPARKY_PROXY_SECRET ?? '';

const tlsAgent = new https.Agent({ rejectUnauthorized: false, keepAlive: true });

const HOP  = new Set([
  'connection','keep-alive','transfer-encoding','upgrade','host',
  'proxy-authenticate','proxy-authorization','te','trailers',
]);
const CORS = new Set([
  'access-control-allow-origin','access-control-allow-methods',
  'access-control-allow-headers','access-control-max-age',
  'access-control-expose-headers','access-control-allow-credentials',
]);

function err(res: ServerResponse, status: number, msg: string) {
  if (!res.headersSent) {
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: msg }));
  }
}

export default function handler(req: IncomingMessage & { url?: string }, res: ServerResponse) {
  if (!BACKEND) return err(res, 503, 'Backend not configured');

  const fwdHeaders: Record<string, string> = { origin: ORIGIN };
  for (const [k, v] of Object.entries(req.headers)) {
    if (!HOP.has(k.toLowerCase()))
      fwdHeaders[k] = Array.isArray(v) ? v[0] : (v ?? '');
  }
  if (KEY)          fwdHeaders['x-sparky-key']    = KEY;
  if (PROXY_SECRET) fwdHeaders['x-proxy-secret']  = PROXY_SECRET;

  const chunks: Buffer[] = [];
  req.on('data', (c: Buffer) => chunks.push(c));
  req.on('error', () => err(res, 400, 'Bad request'));
  req.on('end', () => {
    const body   = Buffer.concat(chunks);
    const target = new URL(req.url ?? '/', BACKEND);
    const isHttps = target.protocol === 'https:';

    const opts: http.RequestOptions = {
      hostname : target.hostname,
      port     : target.port ? Number(target.port) : (isHttps ? 443 : 80),
      path     : target.pathname + target.search,
      method   : req.method ?? 'GET',
      headers  : fwdHeaders,
      agent    : isHttps ? tlsAgent : undefined,
      timeout  : 10_000,
    };

    const pr = (isHttps ? https : http).request(opts, (upstream) => {
      const outHeaders: Record<string, string | string[]> = {};
      for (const [k, v] of Object.entries(upstream.headers)) {
        if (!HOP.has(k) && !CORS.has(k) && v !== undefined)
          outHeaders[k] = v as string | string[];
      }
      res.writeHead(upstream.statusCode ?? 200, outHeaders);
      upstream.pipe(res, { end: true });
    });

    pr.on('timeout', () => pr.destroy());
    pr.on('error',   () => err(res, 502, 'Backend connection failed'));

    if (body.length) pr.write(body);
    pr.end();
  });
}
