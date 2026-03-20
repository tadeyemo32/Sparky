import React, { createContext, useCallback, useContext, useEffect, useRef, useState } from 'react';
import type { Role, AuthResponse } from '../types';
import { logout as apiLogout } from '../api/auth';

const STORAGE_KEY = 'sparky_auth';

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
  const [user, setUser] = useState<AuthUser | null>(() => {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const parsed: AuthUser = JSON.parse(raw);
        if (parsed.expiresAt && Number(parsed.expiresAt) * 1000 > Date.now()) return parsed;
        localStorage.removeItem(STORAGE_KEY);
      }
    } catch { /* ignore */ }
    return null;
  });

  const expiryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // logout depends on [user] — matches original pattern required by tests
  const logout = useCallback(async () => {
    if (expiryTimerRef.current) {
      clearTimeout(expiryTimerRef.current);
      expiryTimerRef.current = null;
    }
    try { localStorage.removeItem(STORAGE_KEY); } catch { /* ignore */ }
    if (user?.token) {
      apiLogout(user.token).catch(() => {/* best-effort */});
    }
    setUser(null);
  }, [user]);

  const login = useCallback((response: AuthResponse) => {
    const u: AuthUser = {
      token: response.token,
      username: response.username,
      role: response.role,
      expiresAt: response.expiresAt,
    };
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(u)); } catch { /* ignore */ }
    // Replace any prior timer with a new one for this session
    if (expiryTimerRef.current) clearTimeout(expiryTimerRef.current);
    if (u.expiresAt) {
      const ms = Number(u.expiresAt) * 1000 - Date.now();
      // setTimeout delay is clamped to INT32_MAX (~24.8 days) in JS engines;
      // skip the timer for sessions that expire further out — expiry will be
      // enforced on the next page load via the localStorage lazy initializer.
      const MAX_TIMER_MS = 2147483647;
      if (ms > 0 && ms <= MAX_TIMER_MS) {
        expiryTimerRef.current = setTimeout(() => {
          try { localStorage.removeItem(STORAGE_KEY); } catch { /* ignore */ }
          setUser(null);
        }, ms);
      }
    }
    setUser(u);
  }, []);

  // On mount: if session was restored from localStorage, schedule its expiry timer.
  // Runs once — uses the initial user captured by the lazy useState initializer.
  useEffect(() => {
    if (!user?.expiresAt || expiryTimerRef.current) return;
    const ms = Number(user.expiresAt) * 1000 - Date.now();
    if (ms <= 0) {
      try { localStorage.removeItem(STORAGE_KEY); } catch { /* ignore */ }
      setUser(null);
      return;
    }
    const MAX_TIMER_MS = 2147483647;
    if (ms <= MAX_TIMER_MS) {
      expiryTimerRef.current = setTimeout(() => {
        try { localStorage.removeItem(STORAGE_KEY); } catch { /* ignore */ }
        setUser(null);
      }, ms);
    }
    return () => {
      if (expiryTimerRef.current) clearTimeout(expiryTimerRef.current);
    };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

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
