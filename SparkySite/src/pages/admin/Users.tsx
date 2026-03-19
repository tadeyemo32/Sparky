import React, { useCallback, useEffect, useState } from 'react';
import { getUsers, banUser, unbanUser } from '../../api/admin';
import { useAuth } from '../../contexts/AuthContext';
import type { User } from '../../types';
import styles from './Admin.module.css';

export function AdminUsers() {
  const { user } = useAuth();
  const [users, setUsers] = useState<User[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [actionError, setActionError] = useState<string | null>(null);
  const [pendingAction, setPendingAction] = useState<string | null>(null);

  const load = useCallback(async () => {
    if (!user?.token) return;
    setError(null);
    try {
      const data = await getUsers(user.token);
      setUsers(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to load users.');
    } finally {
      setLoading(false);
    }
  }, [user?.token]);

  useEffect(() => {
    load();
  }, [load]);

  async function handleBan(hwid: string, currentlyBanned: boolean) {
    if (!user?.token) return;
    setPendingAction(hwid);
    setActionError(null);
    try {
      if (currentlyBanned) {
        await unbanUser(user.token, hwid);
      } else {
        await banUser(user.token, hwid);
      }
      await load();
    } catch (err) {
      setActionError(err instanceof Error ? err.message : 'Action failed.');
    } finally {
      setPendingAction(null);
    }
  }

  function formatDate(iso: string) {
    try {
      return new Date(iso).toLocaleString(undefined, {
        dateStyle: 'short',
        timeStyle: 'short',
      });
    } catch {
      return iso;
    }
  }

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <div>
          <h1 className={styles.title}>User Management</h1>
          <p className={styles.subtitle}>
            View and manage all registered users. Server validates all role permissions.
          </p>
        </div>
        <button onClick={load} className={styles.refreshBtn} disabled={loading}>
          {loading ? 'Loading…' : 'Refresh'}
        </button>
      </div>

      {error && <div className={styles.errorBanner}>{error}</div>}
      {actionError && <div className={styles.errorBanner}>{actionError}</div>}

      <div className={styles.tableWrapper}>
        <table className={styles.table}>
          <thead>
            <tr>
              <th>Username</th>
              <th>HWID (partial)</th>
              <th>License (masked)</th>
              <th>Status</th>
              <th>Last Seen</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {users.length === 0 && !loading && (
              <tr>
                <td colSpan={6} className={styles.emptyCell}>
                  No users found.
                </td>
              </tr>
            )}
            {users.map((u) => (
              <tr key={u.id} className={u.isBanned ? styles.bannedRow : ''}>
                <td className={styles.usernameCell}>{u.username}</td>
                <td className={styles.monoCell}>{u.hwid}</td>
                <td className={styles.monoCell}>{u.licenseKey}</td>
                <td>
                  <span className={u.isBanned ? styles.bannedBadge : styles.activeBadge}>
                    {u.isBanned ? 'Banned' : 'Active'}
                  </span>
                </td>
                <td className={styles.dateCell}>{formatDate(u.lastSeen)}</td>
                <td>
                  <button
                    onClick={() => handleBan(u.hwid, u.isBanned)}
                    disabled={pendingAction === u.hwid}
                    className={u.isBanned ? styles.unbanBtn : styles.banBtn}
                  >
                    {pendingAction === u.hwid
                      ? '…'
                      : u.isBanned
                      ? 'Unban'
                      : 'Ban'}
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
