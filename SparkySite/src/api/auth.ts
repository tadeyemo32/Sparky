import { apiPost, apiGet } from './client';
import type { AuthResponse, MeResponse, SignupPendingResponse } from '../types';

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

export async function signup(
  username: string,
  email: string,
  password: string
): Promise<SignupPendingResponse> {
  const passwordHash = await sha256Hex(password);
  return apiPost<SignupPendingResponse>('/api/auth/signup', { username, email, passwordHash });
}

export async function verifyOtp(username: string, otp: string): Promise<AuthResponse> {
  return apiPost<AuthResponse>('/api/auth/verify-otp', { username, otp });
}

export async function forgotPassword(email: string): Promise<void> {
  return apiPost<void>('/api/auth/forgot-password', { email });
}

export async function resetPassword(
  username: string,
  otp: string,
  newPassword: string
): Promise<void> {
  const newPasswordHash = await sha256Hex(newPassword);
  return apiPost<void>('/api/auth/reset-password', { username, otp, newPasswordHash });
}

export async function logout(token: string): Promise<void> {
  return apiPost<void>('/api/auth/logout', {}, token);
}

export async function getMe(token: string, signal?: AbortSignal): Promise<MeResponse> {
  return apiGet<MeResponse>('/api/auth/me', token, signal);
}
