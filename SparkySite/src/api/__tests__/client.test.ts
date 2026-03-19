import { describe, it, expect, vi, afterEach } from 'vitest';
import { apiFetch, ApiError } from '../client';

function makeFetchMock(body: unknown, status: number, contentLength?: string) {
  const headers = new Map<string, string>();
  if (contentLength !== undefined) headers.set('content-length', contentLength);
  return vi.fn().mockResolvedValue({
    ok: status >= 200 && status < 300,
    status,
    headers: { get: (k: string) => headers.get(k) ?? null },
    json: () => Promise.resolve(body),
    blob: () => Promise.resolve(new Blob(['data'])),
  });
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('apiFetch', () => {
  it('includes x-sparky-key header on every request', async () => {
    vi.stubGlobal('fetch', makeFetchMock({ ok: true }, 200));
    await apiFetch('/test');
    const [, init] = vi.mocked(fetch).mock.calls[0];
    expect((init as RequestInit).headers).toMatchObject({ 'x-sparky-key': expect.any(String) });
  });

  it('adds Authorization header when token provided', async () => {
    vi.stubGlobal('fetch', makeFetchMock({}, 200));
    await apiFetch('/test', { token: 'abc123' });
    const [, init] = vi.mocked(fetch).mock.calls[0];
    expect((init as RequestInit).headers).toMatchObject({ Authorization: 'Bearer abc123' });
  });

  it('does NOT add Authorization header when token is null', async () => {
    vi.stubGlobal('fetch', makeFetchMock({}, 200));
    await apiFetch('/test', { token: null });
    const [, init] = vi.mocked(fetch).mock.calls[0];
    expect((init as RequestInit).headers).not.toHaveProperty('Authorization');
  });

  it('sends Content-Type: application/json when body provided', async () => {
    vi.stubGlobal('fetch', makeFetchMock({}, 200));
    await apiFetch('/test', { method: 'POST', body: { x: 1 } });
    const [, init] = vi.mocked(fetch).mock.calls[0];
    expect((init as RequestInit).headers).toMatchObject({ 'Content-Type': 'application/json' });
  });

  it('serializes body to JSON string', async () => {
    vi.stubGlobal('fetch', makeFetchMock({}, 200));
    await apiFetch('/test', { method: 'POST', body: { foo: 'bar' } });
    const [, init] = vi.mocked(fetch).mock.calls[0];
    expect((init as RequestInit).body).toBe('{"foo":"bar"}');
  });

  it('throws ApiError with status and message on non-ok response', async () => {
    vi.stubGlobal('fetch', makeFetchMock({ message: 'not found' }, 404));
    await expect(apiFetch('/missing')).rejects.toMatchObject({
      name: 'ApiError',
      status: 404,
      message: 'not found',
    });
  });

  it('falls back to generic message when error body has no message field', async () => {
    vi.stubGlobal('fetch', makeFetchMock({ code: 'ERR' }, 500));
    await expect(apiFetch('/boom')).rejects.toMatchObject({
      name: 'ApiError',
      status: 500,
    });
  });

  it('returns undefined for 200 with content-length: 0', async () => {
    vi.stubGlobal('fetch', makeFetchMock(null, 200, '0'));
    const result = await apiFetch('/empty');
    expect(result).toBeUndefined();
  });

  it('returns a Blob when responseType is blob', async () => {
    vi.stubGlobal('fetch', makeFetchMock(null, 200));
    const result = await apiFetch('/file', { responseType: 'blob' });
    expect(result).toBeInstanceOf(Blob);
  });
});

describe('ApiError', () => {
  it('has name ApiError and exposes status', () => {
    const err = new ApiError(403, 'forbidden');
    expect(err.name).toBe('ApiError');
    expect(err.status).toBe(403);
    expect(err.message).toBe('forbidden');
    expect(err).toBeInstanceOf(Error);
  });
});
