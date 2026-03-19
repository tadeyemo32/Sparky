export type Role = 'user' | 'admin' | 'owner';

export interface AuthResponse {
  token: string;
  username: string;
  role: Role;
  expiresAt: string;
}

export interface MeResponse {
  username: string;
  role: Role;
  hwid: string;
  licenseKey: string;
  expiresAt: string;
}

export interface User {
  id: string;
  username: string;
  hwid: string;
  licenseKey: string;
  isBanned: boolean;
  lastSeen: string;
}

export interface License {
  key: string;
  tier: number;
  hwid: string;
  expiresAt: string;
  isBound: boolean;
}

export interface Admin {
  username: string;
  hwid: string;
  grantedAt: string;
}

export interface SessionsResponse {
  count: number;
}

export interface IssueLicenseResponse {
  key: string;
}

export interface AuthState {
  token: string | null;
  username: string | null;
  role: Role | null;
  expiresAt: string | null;
}
