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
          // 401 = backend responded (not authed, but alive)
          // 2xx = also fine; anything else (404 from dev server, 5xx from proxy) = offline
          const alive = res.status === 401 || (res.status >= 200 && res.status < 300);
          setStatus(alive ? 'online' : 'offline');
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
