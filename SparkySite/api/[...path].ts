/**
 * Vercel Serverless Proxy — /api/*
 *
 * All /api/* requests from the browser are forwarded server-side to the
 * backend. The backend URL and SPARKY_KEY are read from Vercel environment
 * variables — neither ever appears in the browser bundle or network traffic.
 *
 * TLS verification is intentionally disabled for the backend connection
 * because the backend uses a self-signed certificate (we own both ends).
 */
import type { VercelRequest, VercelResponse } from '@vercel/node';
import https from 'node:https';
import http from 'node:http';
import { URL } from 'node:url';

export const config = {
  api: {
    bodyParser: false, // receive raw body so we can pipe it unmodified
  },
};

const BACKEND_URL     = (process.env.BACKEND_URL ?? '').replace(/\/$/, '');
const SPARKY_KEY      = process.env.SPARKY_KEY ?? '';
const ALLOWED_ORIGIN  = process.env.SPARKY_ALLOWED_ORIGIN ?? 'https://sparky-tau.vercel.app';

// Reuse agent across requests — skips cert verification for our self-signed backend
const tlsAgent = new https.Agent({ rejectUnauthorized: false, keepAlive: true });

// Headers the proxy must not forward upstream or downstream
const HOP_BY_HOP = new Set([
  'connection', 'keep-alive', 'proxy-authenticate', 'proxy-authorization',
  'te', 'trailers', 'transfer-encoding', 'upgrade', 'host',
]);
// CORS headers from the backend — the browser sees sparky-tau.vercel.app as
// the origin so we must not forward the backend's own CORS declarations.
const STRIP_DOWNSTREAM = new Set([
  'access-control-allow-origin', 'access-control-allow-methods',
  'access-control-allow-headers', 'access-control-max-age',
  'access-control-expose-headers', 'access-control-allow-credentials',
]);

export default async function handler(req: VercelRequest, res: VercelResponse) {
  if (!BACKEND_URL) {
    return res.status(503).json({ error: 'Backend not configured' });
  }

  // req.url is the full path, e.g. /api/auth/login?foo=bar
  const targetUrl = new URL(req.url ?? '/', BACKEND_URL);

  // Build forwarded headers
  const fwdHeaders: Record<string, string> = {};
  for (const [k, v] of Object.entries(req.headers)) {
    if (HOP_BY_HOP.has(k.toLowerCase())) continue;
    fwdHeaders[k] = Array.isArray(v) ? v[0] : (v ?? '');
  }
  if (SPARKY_KEY) fwdHeaders['x-sparky-key'] = SPARKY_KEY;

  // Always set Origin so the backend's origin check passes for all methods
  // (browsers omit Origin on GET/HEAD; the proxy always knows the true origin).
  fwdHeaders['origin'] = ALLOWED_ORIGIN;

  // Read raw request body
  const body = await new Promise<Buffer>((resolve) => {
    const chunks: Buffer[] = [];
    req.on('data', (c: Buffer) => chunks.push(c));
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', () => resolve(Buffer.alloc(0)));
  });

  return new Promise<void>((resolve) => {
    const isHttps = targetUrl.protocol === 'https:';
    const port    = targetUrl.port
      ? Number(targetUrl.port)
      : isHttps ? 443 : 80;

    const opts: https.RequestOptions = {
      hostname : targetUrl.hostname,
      port,
      path     : targetUrl.pathname + targetUrl.search,
      method   : req.method ?? 'GET',
      headers  : fwdHeaders,
      agent    : isHttps ? tlsAgent : undefined,
      timeout  : 10000, // 10 s — fail fast rather than hanging
    };

    const mod = isHttps ? https : http;
    const proxyReq = mod.request(opts, (proxyRes) => {
      res.status(proxyRes.statusCode ?? 200);

      for (const [k, v] of Object.entries(proxyRes.headers)) {
        if (HOP_BY_HOP.has(k.toLowerCase())) continue;
        if (STRIP_DOWNSTREAM.has(k.toLowerCase())) continue;
        if (v !== undefined) res.setHeader(k, v as string | string[]);
      }

      proxyRes.pipe(res, { end: true });
      proxyRes.on('end', resolve);
    });

    proxyReq.on('timeout', () => proxyReq.destroy());
    proxyReq.on('error', () => {
      if (!res.headersSent) res.status(502).json({ error: 'Backend connection failed' });
      resolve();
    });

    if (body.length > 0) proxyReq.write(body);
    proxyReq.end();
  });
}
