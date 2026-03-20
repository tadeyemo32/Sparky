// API calls are routed through Vercel's proxy (vercel.json rewrites).
// The browser only ever talks to the Vercel domain — the backend IP is
// never exposed in the bundle or in browser network traffic.
const API_BASE = '';

export interface RequestOptions {
  method?: string;
  body?: unknown;
  token?: string | null;
  responseType?: 'json' | 'blob';
  signal?: AbortSignal;
}

export class ApiError extends Error {
  constructor(
    public status: number,
    message: string
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

export async function apiFetch<T>(
  path: string,
  options: RequestOptions = {}
): Promise<T> {
  const { method = 'GET', body, token, responseType = 'json', signal } = options;

  const headers: Record<string, string> = {};

  if (body !== undefined) {
    headers['Content-Type'] = 'application/json';
  }

  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

  const res = await fetch(`${API_BASE}${path}`, {
    method,
    headers,
    body: body !== undefined ? JSON.stringify(body) : undefined,
    signal,
  });

  if (!res.ok) {
    let message = `Request failed with status ${res.status}`;
    try {
      const data = await res.json();
      if (typeof data === 'object' && data !== null && 'message' in data) {
        message = String((data as Record<string, unknown>)['message']);
      } else if (typeof data === 'object' && data !== null && 'error' in data) {
        message = String((data as Record<string, unknown>)['error']);
      }
    } catch {
      // ignore parse errors
    }
    throw new ApiError(res.status, message);
  }

  if (responseType === 'blob') {
    return res.blob() as Promise<T>;
  }

  if (res.status === 200 && res.headers.get('content-length') === '0') {
    return undefined as T;
  }

  try {
    return (await res.json()) as T;
  } catch {
    return undefined as T;
  }
}

export async function apiGet<T>(
  path: string,
  token?: string | null,
  signal?: AbortSignal
): Promise<T> {
  return apiFetch<T>(path, { method: 'GET', token, signal });
}

export async function apiPost<T>(
  path: string,
  body: unknown,
  token?: string | null,
  signal?: AbortSignal
): Promise<T> {
  return apiFetch<T>(path, { method: 'POST', body, token, signal });
}

export async function apiGetBlob(path: string, token?: string | null): Promise<Blob> {
  return apiFetch<Blob>(path, { method: 'GET', token, responseType: 'blob' });
}

export { API_BASE };
