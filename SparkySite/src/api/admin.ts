import { apiGet, apiPost } from './client';
import type { User, License, Admin, SessionsResponse, IssueLicenseResponse, MetricsResponse } from '../types';

// --- Admin: Users ---

export async function getUsers(token: string, signal?: AbortSignal): Promise<User[]> {
  return apiGet<User[]>('/api/admin/users', token, signal);
}

export async function banUser(token: string, hwid: string): Promise<void> {
  return apiPost<void>('/api/admin/users/ban', { hwid }, token);
}

export async function unbanUser(token: string, hwid: string): Promise<void> {
  return apiPost<void>('/api/admin/users/unban', { hwid }, token);
}

// --- Admin: Licenses ---

export async function getLicenses(token: string, signal?: AbortSignal): Promise<License[]> {
  return apiGet<License[]>('/api/admin/licenses', token, signal);
}

export async function issueLicense(
  token: string,
  tier: number,
  days: number
): Promise<IssueLicenseResponse> {
  return apiPost<IssueLicenseResponse>('/api/admin/licenses/issue', { tier, days }, token);
}

// --- Admin: Sessions ---

export async function getSessions(token: string, signal?: AbortSignal): Promise<SessionsResponse> {
  return apiGet<SessionsResponse>('/api/admin/sessions', token, signal);
}

// --- Owner: Admins ---

export async function getAdmins(token: string, signal?: AbortSignal): Promise<Admin[]> {
  return apiGet<Admin[]>('/api/owner/admins', token, signal);
}

export async function grantAdmin(token: string, username: string): Promise<void> {
  return apiPost<void>('/api/owner/admins/grant', { username }, token);
}

export async function revokeAdmin(token: string, username: string): Promise<void> {
  return apiPost<void>('/api/owner/admins/revoke', { username }, token);
}

// --- Owner: Metrics ---

export async function getMetrics(token: string, signal?: AbortSignal): Promise<MetricsResponse> {
  return apiGet<MetricsResponse>('/api/owner/metrics', token, signal);
}
