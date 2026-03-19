const API_BASE = import.meta.env.VITE_API_BASE ?? 'https://35.206.181.36';
const SPARKY_KEY = 'VhPuLNayUPLTtOkOMoChbnaKHexOCetJaa4iXkLDF2s=';

export interface RequestOptions {
  method?: string;
  body?: unknown;
  token?: string | null;
  responseType?: 'json' | 'blob';
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
  const { method = 'GET', body, token, responseType = 'json' } = options;

  const headers: Record<string, string> = {
    'x-sparky-key': SPARKY_KEY,
  };

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

export async function apiGet<T>(path: string, token?: string | null): Promise<T> {
  return apiFetch<T>(path, { method: 'GET', token });
}

export async function apiPost<T>(
  path: string,
  body: unknown,
  token?: string | null
): Promise<T> {
  return apiFetch<T>(path, { method: 'POST', body, token });
}

export async function apiGetBlob(path: string, token?: string | null): Promise<Blob> {
  return apiFetch<Blob>(path, { method: 'GET', token, responseType: 'blob' });
}

export { API_BASE };
