import type { VercelRequest, VercelResponse } from '@vercel/node';

export const config = { runtime: 'nodejs' };

export default async function handler(req: VercelRequest, res: VercelResponse) {
  const backendUrl = process.env.BACKEND_URL?.replace(/\/$/, '');
  const proxySecret = process.env.SPARKY_PROXY_SECRET;

  if (!backendUrl) {
    return res.status(503).json({ message: 'Backend not configured' });
  }

  // Build upstream URL: /api/auth/login -> backendUrl/api/auth/login
  const pathSegments = Array.isArray(req.query.path)
    ? req.query.path.join('/')
    : (req.query.path ?? '');

  const qs = req.url?.includes('?') ? req.url.slice(req.url.indexOf('?')) : '';
  const upstream = `${backendUrl}/api/${pathSegments}${qs}`;

  // Forward safe request headers
  const forwardHeaders: Record<string, string> = {};

  const ct = req.headers['content-type'];
  if (ct) forwardHeaders['Content-Type'] = Array.isArray(ct) ? ct[0] : ct;

  const auth = req.headers['authorization'];
  if (auth) forwardHeaders['Authorization'] = Array.isArray(auth) ? auth[0] : auth;

  if (proxySecret) {
    forwardHeaders['X-Proxy-Secret'] = proxySecret;
  }

  const hasBody = req.method !== 'GET' && req.method !== 'HEAD';

  let upstreamRes: Response;
  try {
    upstreamRes = await fetch(upstream, {
      method: req.method ?? 'GET',
      headers: forwardHeaders,
      body: hasBody ? JSON.stringify(req.body) : undefined,
    });
  } catch {
    return res.status(502).json({ message: 'Backend unreachable' });
  }

  // Forward response headers (skip hop-by-hop)
  const skip = new Set(['transfer-encoding', 'connection', 'keep-alive', 'te', 'trailers', 'upgrade']);
  upstreamRes.headers.forEach((value, key) => {
    if (!skip.has(key.toLowerCase())) res.setHeader(key, value);
  });

  res.status(upstreamRes.status);

  const buf = await upstreamRes.arrayBuffer();
  res.send(Buffer.from(buf));
}
