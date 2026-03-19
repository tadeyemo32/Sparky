import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { sha256Hex, login, signup, logout, getMe } from '../auth';

// ---------------------------------------------------------------------------
// sha256Hex — unit test (uses real Web Crypto via jsdom)
// ---------------------------------------------------------------------------
describe('sha256Hex', () => {
  it('returns a 64-char lowercase hex string', async () => {
    const hash = await sha256Hex('password123');
    expect(hash).toHaveLength(64);
    expect(hash).toMatch(/^[0-9a-f]+$/);
  });

  it('returns deterministic output for the same input', async () => {
    const a = await sha256Hex('hello');
    const b = await sha256Hex('hello');
    expect(a).toBe(b);
  });

  it('returns different hashes for different inputs', async () => {
    const a = await sha256Hex('hello');
    const b = await sha256Hex('world');
    expect(a).not.toBe(b);
  });

  it('matches known SHA-256 value for empty string', async () => {
    const hash = await sha256Hex('');
    expect(hash).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  });
});

// ---------------------------------------------------------------------------
// API call tests — fetch is mocked
// ---------------------------------------------------------------------------
function mockFetch(body: unknown, status = 200) {
  const response = {
    ok: status >= 200 && status < 300,
    status,
    headers: { get: () => null },
    json: () => Promise.resolve(body),
    blob: () => Promise.resolve(new Blob()),
  };
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response));
}

function mockFetchError(status: number, message: string) {
  const response = {
    ok: false,
    status,
    headers: { get: () => null },
    json: () => Promise.resolve({ message }),
    blob: () => Promise.resolve(new Blob()),
  };
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(response));
}

beforeEach(() => {
  vi.restoreAllMocks();
});

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('login', () => {
  it('sends passwordHash (not plaintext) in request body', async () => {
    const fakeResp = { token: 'tok', username: 'alice', role: 'user', expiresAt: '9999' };
    mockFetch(fakeResp);

    await login('alice', 'password123');

    const fetchMock = vi.mocked(fetch);
    expect(fetchMock).toHaveBeenCalledOnce();
    const [, init] = fetchMock.mock.calls[0];
    const body = JSON.parse((init as RequestInit).body as string);
    expect(body.username).toBe('alice');
    expect(body.passwordHash).toHaveLength(64);
    expect(body.passwordHash).not.toBe('password123');
    expect(body).not.toHaveProperty('password');
  });

  it('posts to /api/auth/login', async () => {
    mockFetch({ token: 't', username: 'u', role: 'user', expiresAt: '0' });
    await login('alice', 'pass');
    const [url] = vi.mocked(fetch).mock.calls[0];
    expect(String(url)).toContain('/api/auth/login');
  });

  it('throws on 401', async () => {
    mockFetchError(401, 'invalid credentials');
    await expect(login('alice', 'wrong')).rejects.toThrow('invalid credentials');
  });
});

describe('signup', () => {
  it('does NOT send licenseKey in request body', async () => {
    mockFetch({ token: 't', username: 'bob', role: 'user', expiresAt: '0' });
    await signup('bob', '', 'password123');

    const [, init] = vi.mocked(fetch).mock.calls[0];
    const body = JSON.parse((init as RequestInit).body as string);
    expect(body).not.toHaveProperty('licenseKey');
    expect(body.username).toBe('bob');
    expect(body.passwordHash).toHaveLength(64);
  });

  it('posts to /api/auth/signup', async () => {
    mockFetch({ token: 't', username: 'bob', role: 'user', expiresAt: '0' });
    await signup('bob', '', 'password123');
    const [url] = vi.mocked(fetch).mock.calls[0];
    expect(String(url)).toContain('/api/auth/signup');
  });

  it('throws 409 on duplicate username', async () => {
    mockFetchError(409, 'username already taken');
    await expect(signup('bob', '', 'password123')).rejects.toThrow('username already taken');
  });
});

describe('logout', () => {
  it('posts to /api/auth/logout with bearer token', async () => {
    mockFetch(undefined, 200);
    await logout('my-token');

    const [url, init] = vi.mocked(fetch).mock.calls[0];
    expect(String(url)).toContain('/api/auth/logout');
    expect((init as RequestInit).headers).toMatchObject({
      Authorization: 'Bearer my-token',
    });
  });
});

describe('getMe', () => {
  it('sends bearer token and calls /api/auth/me', async () => {
    mockFetch({ username: 'alice', role: 'user', hwid: '', licenseKey: '', expiresAt: '' });
    await getMe('tok-abc');

    const [url, init] = vi.mocked(fetch).mock.calls[0];
    expect(String(url)).toContain('/api/auth/me');
    expect((init as RequestInit).headers).toMatchObject({ Authorization: 'Bearer tok-abc' });
  });
});
