import { useEffect, useState } from 'react';
import { API_BASE } from '../api/client';

export type ServerStatus = 'checking' | 'online' | 'offline';

const POLL_INTERVAL_MS = 30_000;

export function useServerStatus(): ServerStatus {
  const [status, setStatus] = useState<ServerStatus>('checking');

  useEffect(() => {
    let cancelled = false;

    async function check() {
      try {
        // Probe a backend-only route. Any <500 response means the server (or its
        // proxy) is reachable: 401 = unauthenticated, 403 = proxy secret check
        // passed to backend. 5xx = backend/proxy down; thrown = no connection.
        const res = await fetch(`${API_BASE}/api/auth/me`, { method: 'GET' });
        if (!cancelled) {
          setStatus(res.status < 500 ? 'online' : 'offline');
        }
      } catch {
        if (!cancelled) {
          setStatus('offline');
        }
      }
    }

    check();
    const id = setInterval(check, POLL_INTERVAL_MS);

    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, []);

  return status;
}
