import React, { useCallback, useEffect, useState } from 'react';
import { getAdmins, grantAdmin, revokeAdmin } from '../../api/admin';
import { useAuth } from '../../contexts/AuthContext';
import type { Admin } from '../../types';
import styles from './Admin.module.css';

export function OwnerAdmins() {
  const { user } = useAuth();
  const [admins, setAdmins] = useState<Admin[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [actionError, setActionError] = useState<string | null>(null);
  const [pendingAction, setPendingAction] = useState<string | null>(null);

  // Grant form
  const [grantUsername, setGrantUsername] = useState('');
  const [grantLoading, setGrantLoading] = useState(false);
  const [grantError, setGrantError] = useState<string | null>(null);
  const [grantSuccess, setGrantSuccess] = useState(false);

  const load = useCallback(async () => {
    if (!user?.token) return;
    setError(null);
    try {
      const data = await getAdmins(user.token);
      setAdmins(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load admins.');
    } finally {
      setLoading(false);
    }
  }, [user?.token]);

  useEffect(() => {
    load();
  }, [load]);

  async function handleRevoke(username: string) {
    if (!user?.token) return;
    setPendingAction(username);
    setActionError(null);
    try {
      await revokeAdmin(user.token, username);
      await load();
    } catch (err) {
      setActionError(err instanceof Error ? err.message : 'Revoke failed.');
    } finally {
      setPendingAction(null);
    }
  }

  async function handleGrant(e: React.FormEvent) {
    e.preventDefault();
    if (!user?.token) return;
    const uname = grantUsername.trim();
    if (!uname) { setGrantError('Username is required.'); return; }
    setGrantLoading(true);
    setGrantError(null);
    setGrantSuccess(false);
    try {
      await grantAdmin(user.token, uname);
      setGrantUsername('');
      setGrantSuccess(true);
      setTimeout(() => setGrantSuccess(false), 3000);
      await load();
    } catch (err) {
      setGrantError(err instanceof Error ? err.message : 'Grant failed.');
    } finally {
      setGrantLoading(false);
    }
  }

  function formatDate(ts: string) {
    const n = parseInt(ts, 10);
    if (!n) return '—';
    return new Date(n * 1000).toLocaleDateString(undefined, {
      year: 'numeric', month: 'short', day: 'numeric',
    });
  }

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <div>
          <h1 className={styles.title}>Admin Management</h1>
          <p className={styles.subtitle}>
            Grant and revoke admin privileges by username. Owner access only — the server enforces
            this.
          </p>
        </div>
        <button onClick={load} className={styles.refreshBtn} disabled={loading}>
          {loading ? 'Loading…' : 'Refresh'}
        </button>
      </div>

      {error && <div className={styles.errorBanner}>{error}</div>}
      {actionError && <div className={styles.errorBanner}>{actionError}</div>}

      <div className={styles.grantCard}>
        <h2 className={styles.sectionTitle}>Grant Admin</h2>
        <form onSubmit={handleGrant} className={styles.grantForm}>
          {grantError && <div className={styles.errorBanner}>{grantError}</div>}
          {grantSuccess && (
            <div className={styles.successBanner}>Admin granted successfully.</div>
          )}
          <div className={styles.grantRow}>
            <input
              type="text"
              className={styles.input}
              placeholder="Enter username"
              value={grantUsername}
              onChange={(e) => setGrantUsername(e.target.value)}
              disabled={grantLoading}
            />
            <button type="submit" className={styles.primaryBtn} disabled={grantLoading}>
              {grantLoading ? 'Granting…' : 'Grant Admin'}
            </button>
          </div>
        </form>
      </div>

      <div className={styles.tableWrapper}>
        <table className={styles.table}>
          <thead>
            <tr>
              <th>Username</th>
              <th>Role</th>
              <th>Member Since</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {admins.length === 0 && !loading && (
              <tr>
                <td colSpan={4} className={styles.emptyCell}>No admins found.</td>
              </tr>
            )}
            {admins.map((admin) => (
              <tr key={admin.username}>
                <td className={styles.usernameCell}>{admin.username}</td>
                <td>
                  <span style={{ fontSize: '0.7rem', textTransform: 'uppercase',
                    letterSpacing: '0.08em', color: '#888', fontWeight: 600 }}>
                    {(admin as Admin & { role?: string }).role || 'admin'}
                  </span>
                </td>
                <td className={styles.dateCell}>{formatDate(admin.grantedAt)}</td>
                <td>
                  <button
                    onClick={() => handleRevoke(admin.username)}
                    disabled={pendingAction === admin.username}
                    className={styles.banBtn}
                  >
                    {pendingAction === admin.username ? '…' : 'Revoke'}
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
