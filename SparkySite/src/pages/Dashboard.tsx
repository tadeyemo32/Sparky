import React, { useEffect, useState } from 'react';
import { getMe } from '../api/auth';
import { useAuth } from '../contexts/AuthContext';
import type { MeResponse } from '../types';
import styles from './Dashboard.module.css';

export function Dashboard() {
  const { user } = useAuth();
  const [me, setMe] = useState<MeResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    if (!user?.token) return;
    const controller = new AbortController();

    getMe(user.token, controller.signal)
      .then(setMe)
      .catch((err: Error) => { if (err.name !== 'AbortError') setError(err.message); })
      .finally(() => setLoading(false));

    return () => controller.abort();
  }, [user?.token]);

  function formatDate(iso: string) {
    try {
      return new Date(iso).toLocaleDateString(undefined, {
        year: 'numeric',
        month: 'long',
        day: 'numeric',
      });
    } catch {
      return iso;
    }
  }

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <h1 className={styles.title}>Dashboard</h1>
        <p className={styles.subtitle}>Welcome back, {user?.username}</p>
      </div>

      {loading && <div className={styles.loading}>Loading your account info…</div>}
      {error && <div className={styles.errorBanner}>{error}</div>}

      {me && (
        <div className={styles.grid}>
          <div className={styles.card}>
            <div className={styles.cardLabel}>Username</div>
            <div className={styles.cardValue}>{me.username}</div>
          </div>

          <div className={styles.card}>
            <div className={styles.cardLabel}>Role</div>
            <div className={styles.cardValue}>
              <span className={`${styles.badge} ${styles[`badge_${me.role}`]}`}>{me.role}</span>
            </div>
          </div>

          <div className={styles.card}>
            <div className={styles.cardLabel}>Hardware ID (partial)</div>
            <div className={`${styles.cardValue} ${styles.mono}`}>{me.hwid}</div>
          </div>

          <div className={styles.card}>
            <div className={styles.cardLabel}>License Key</div>
            <div className={`${styles.cardValue} ${styles.mono}`}>{me.licenseKey}</div>
          </div>

          <div className={styles.card}>
            <div className={styles.cardLabel}>License Expires</div>
            <div className={styles.cardValue}>{formatDate(me.expiresAt)}</div>
          </div>
        </div>
      )}

      <div className={styles.notice}>
        <span className={styles.noticeIcon}>ℹ️</span>
        Hardware IDs and license keys are partially masked for your security. The server
        validates all requests independently.
      </div>
    </div>
  );
}
