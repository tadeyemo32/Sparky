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
        // Probe a backend-only route. A 401 means the server responded (online);
        // a 5xx means the proxy/backend is down; a thrown error means no connection.
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
