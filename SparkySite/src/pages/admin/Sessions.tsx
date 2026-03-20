import React, { useCallback, useEffect, useRef, useState } from 'react';
import { getSessions } from '../../api/admin';
import { useAuth } from '../../contexts/AuthContext';
import styles from './Admin.module.css';

const REFRESH_INTERVAL_MS = 10_000;

export function AdminSessions() {
  const { user } = useAuth();
  const [count, setCount] = useState<number | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  const load = useCallback(async (signal?: AbortSignal) => {
    if (!user?.token) return;
    setError(null);
    try {
      const data = await getSessions(user.token, signal);
      setCount(data.count);
      setLastUpdated(new Date());
    } catch (err) {
      if (err instanceof Error && err.name !== 'AbortError')
        setError(err.message || 'Failed to load sessions.');
    } finally {
      setLoading(false);
    }
  }, [user?.token]);

  useEffect(() => {
    const controller = new AbortController();
    abortRef.current = controller;
    load(controller.signal);
    intervalRef.current = setInterval(() => {
      if (abortRef.current) abortRef.current.abort();
      abortRef.current = new AbortController();
      load(abortRef.current.signal);
    }, REFRESH_INTERVAL_MS);
    return () => {
      if (intervalRef.current !== null) clearInterval(intervalRef.current);
      if (abortRef.current) abortRef.current.abort();
    };
  }, [load]);

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <div>
          <h1 className={styles.title}>Active Sessions</h1>
          <p className={styles.subtitle}>Auto-refreshes every 10 seconds.</p>
        </div>
        <button onClick={() => load()} className={styles.refreshBtn} disabled={loading}>
          {loading ? 'Loading…' : 'Refresh'}
        </button>
      </div>

      {error && <div className={styles.errorBanner}>{error}</div>}

      <div className={styles.sessionCard}>
        <div className={styles.sessionCount}>
          {loading && count === null ? '—' : (count ?? '—')}
        </div>
        <div className={styles.sessionLabel}>Active Sessions Right Now</div>
        {lastUpdated && (
          <div className={styles.sessionUpdated}>
            Last updated:{' '}
            {lastUpdated.toLocaleTimeString(undefined, { timeStyle: 'medium' })}
          </div>
        )}
      </div>
    </div>
  );
}
