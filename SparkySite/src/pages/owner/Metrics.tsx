import React, { useCallback, useEffect, useRef, useState } from 'react';
import { getMetrics } from '../../api/admin';
import { useAuth } from '../../contexts/AuthContext';
import type { MetricsResponse } from '../../types';
import styles from '../admin/Admin.module.css';

function formatUptime(seconds: number): string {
  if (seconds < 60) return `${seconds}s`;
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return `${d}d ${h}h ${m}m`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m ${seconds % 60}s`;
}

function fmtBytes(bytes: number): string {
  if (!bytes) return '—';
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

export function OwnerMetrics() {
  const { user } = useAuth();
  const [metrics, setMetrics] = useState<MetricsResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [autoRefresh, setAutoRefresh] = useState(true);
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const load = useCallback(async () => {
    if (!user?.token) return;
    try {
      const data = await getMetrics(user.token);
      setMetrics(data);
      setLastUpdated(new Date());
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load metrics.');
    } finally {
      setLoading(false);
    }
  }, [user?.token]);

  useEffect(() => { load(); }, [load]);

  useEffect(() => {
    if (autoRefresh) {
      timerRef.current = setInterval(load, 30_000);
    } else if (timerRef.current) {
      clearInterval(timerRef.current);
    }
    return () => { if (timerRef.current) clearInterval(timerRef.current); };
  }, [autoRefresh, load]);

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <div>
          <h1 className={styles.title}>Server Metrics</h1>
          <p className={styles.subtitle}>
            Live statistics pulled directly from the production instance.
          </p>
        </div>
        <button onClick={load} className={styles.refreshBtn} disabled={loading}>
          {loading ? 'Fetching…' : 'Refresh now'}
        </button>
      </div>

      <div className={styles.autoRefreshRow}>
        <label className={styles.toggleLabel}>
          <input
            type="checkbox"
            checked={autoRefresh}
            onChange={(e) => setAutoRefresh(e.target.checked)}
            style={{ accentColor: '#fff', cursor: 'pointer' }}
          />
          Auto-refresh every 30 s
        </label>
        {lastUpdated && (
          <span className={styles.lastUpdated}>Updated {lastUpdated.toLocaleTimeString()}</span>
        )}
      </div>

      {error && <div className={styles.errorBanner}>{error}</div>}

      {metrics && (
        <div className={styles.metricsGrid}>
          <div className={styles.metricCard}>
            <span className={styles.metricLabel}>Server</span>
            <span className={styles.metricValueSmall}>
              <span className={styles.onlineDot} />Online
            </span>
            <span className={styles.metricSub}>Build {metrics.buildVersion}</span>
          </div>
          <div className={styles.metricCard}>
            <span className={styles.metricLabel}>Active Sessions</span>
            <span className={styles.metricValue}>{metrics.activeSessions}</span>
            <span className={styles.metricSub}>Loader connections</span>
          </div>
          <div className={styles.metricCard}>
            <span className={styles.metricLabel}>Uptime</span>
            <span className={styles.metricValueSmall}>{formatUptime(metrics.uptimeSeconds)}</span>
            <span className={styles.metricSub}>Since last restart</span>
          </div>
          <div className={styles.metricCard}>
            <span className={styles.metricLabel}>DLL Payload</span>
            <span className={styles.metricValueSmall}>
              {metrics.dllLoaded
                ? <><span className={styles.onlineDot} />Loaded</>
                : <span style={{ color: '#ff4444' }}>Not loaded</span>}
            </span>
            <span className={styles.metricSub}>{fmtBytes(metrics.dllSizeBytes)}</span>
          </div>
          <div className={styles.metricCard}>
            <span className={styles.metricLabel}>Total Users</span>
            <span className={styles.metricValue}>{metrics.totalUsers}</span>
            <span className={styles.metricSub}>Registered accounts</span>
          </div>
          <div className={styles.metricCard}>
            <span className={styles.metricLabel}>Total Licenses</span>
            <span className={styles.metricValue}>{metrics.totalLicenses}</span>
            <span className={styles.metricSub}>Issued keys</span>
          </div>
        </div>
      )}
    </div>
  );
}
