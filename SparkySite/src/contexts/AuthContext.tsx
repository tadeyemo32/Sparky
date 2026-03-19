import React, { createContext, useCallback, useContext, useState } from 'react';
import type { Role, AuthResponse } from '../types';
import { logout as apiLogout } from '../api/auth';

interface AuthUser {
  token: string;
  username: string;
  role: Role;
  expiresAt: string;
}

interface AuthContextValue {
  user: AuthUser | null;
  login: (response: AuthResponse) => void;
  logout: () => Promise<void>;
  isAuthenticated: boolean;
  isAdmin: boolean;
  isOwner: boolean;
}

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [user, setUser] = useState<AuthUser | null>(null);

  const login = useCallback((response: AuthResponse) => {
    setUser({
      token: response.token,
      username: response.username,
      role: response.role,
      expiresAt: response.expiresAt,
    });
  }, []);

  const logout = useCallback(async () => {
    if (user?.token) {
      try {
        await apiLogout(user.token);
      } catch {
        // best-effort: clear state regardless
      }
    }
    setUser(null);
  }, [user]);

  const isAuthenticated = user !== null;
  const isAdmin = user?.role === 'admin' || user?.role === 'owner';
  const isOwner = user?.role === 'owner';

  return (
    <AuthContext.Provider value={{ user, login, logout, isAuthenticated, isAdmin, isOwner }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth(): AuthContextValue {
  const ctx = useContext(AuthContext);
  if (!ctx) {
    throw new Error('useAuth must be used inside AuthProvider');
  }
  return ctx;
}
