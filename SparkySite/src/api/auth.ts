import { apiPost, apiGet } from './client';
import type { AuthResponse, MeResponse } from '../types';

export async function sha256Hex(input: string): Promise<string> {
  const encoder = new TextEncoder();
  const data = encoder.encode(input);
  const hashBuffer = await crypto.subtle.digest('SHA-256', data);
  const hashArray = Array.from(new Uint8Array(hashBuffer));
  return hashArray.map((b) => b.toString(16).padStart(2, '0')).join('');
}

export async function login(username: string, password: string): Promise<AuthResponse> {
  const passwordHash = await sha256Hex(password);
  return apiPost<AuthResponse>('/api/auth/login', { username, passwordHash });
}

// Web accounts are independent — no license key required.
// licenseKey param kept for backwards-compat but ignored by the server.
export async function signup(
  username: string,
  _licenseKey: string,
  password: string
): Promise<AuthResponse> {
  const passwordHash = await sha256Hex(password);
  return apiPost<AuthResponse>('/api/auth/signup', { username, passwordHash });
}

export async function logout(token: string): Promise<void> {
  return apiPost<void>('/api/auth/logout', {}, token);
}

export async function getMe(token: string): Promise<MeResponse> {
  return apiGet<MeResponse>('/api/auth/me', token);
}
