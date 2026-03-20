import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { getUsers, banUser, unbanUser } from '../../api/admin';
import { useAuth } from '../../contexts/AuthContext';
import type { User } from '../../types';
import styles from './Admin.module.css';

type Filter = 'all' | 'active' | 'banned';

export function AdminUsers() {
  const { user } = useAuth();
  const [users, setUsers] = useState<User[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [actionError, setActionError] = useState<string | null>(null);
  const [pendingAction, setPendingAction] = useState<string | null>(null);
  const [search, setSearch] = useState('');
  const [filter, setFilter] = useState<Filter>('all');
  const [copied, setCopied] = useState<string | null>(null);

  const load = useCallback(async (signal?: AbortSignal) => {
    if (!user?.token) return;
    setLoading(true);
    setError(null);
    try {
      const data = await getUsers(user.token, signal);
      setUsers(data);
    } catch (err) {
      if (err instanceof Error && err.name !== 'AbortError')
        setError(err.message || 'Failed to load users.');
    } finally {
      setLoading(false);
    }
  }, [user?.token]);

  useEffect(() => {
    const controller = new AbortController();
    load(controller.signal);
    return () => controller.abort();
  }, [load]);

  async function handleBan(hwid: string, currentlyBanned: boolean) {
    if (!user?.token) return;
    setPendingAction(hwid);
    setActionError(null);
    try {
      if (currentlyBanned) await unbanUser(user.token, hwid);
      else await banUser(user.token, hwid);
      await load();
    } catch (err) {
      setActionError(err instanceof Error ? err.message : 'Action failed.');
    } finally {
      setPendingAction(null);
    }
  }

  async function copyHwid(hwid: string) {
    try {
      await navigator.clipboard.writeText(hwid);
      setCopied(hwid);
      setTimeout(() => setCopied(null), 1500);
    } catch { /* ignore */ }
  }

  function formatDate(ts: string) {
    const n = parseInt(ts, 10);
    if (!n) return '—';
    return new Date(n * 1000).toLocaleString(undefined, {
      dateStyle: 'short',
      timeStyle: 'short',
    });
  }

  function maskLicense(key: string) {
    if (!key) return '—';
    const parts = key.split('-');
    if (parts.length === 4) return `${parts[0]}-****-****-${parts[3]}`;
    return key.substring(0, 4) + '-****';
  }

  const filtered = useMemo(() => {
    const q = search.toLowerCase().trim();
    return users.filter((u) => {
      if (filter === 'active' && u.isBanned) return false;
      if (filter === 'banned' && !u.isBanned) return false;
      if (q) {
        return (
          u.username.toLowerCase().includes(q) ||
          u.hwid.toLowerCase().startsWith(q)
        );
      }
      return true;
    });
  }, [users, search, filter]);

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <div>
          <h1 className={styles.title}>User Management</h1>
          <p className={styles.subtitle}>
            {users.length} total users — {users.filter((u) => u.isBanned).length} banned.
            Server enforces all role permissions.
          </p>
        </div>
        <button onClick={() => load()} className={styles.refreshBtn} disabled={loading}>
          {loading ? 'Loading…' : 'Refresh'}
        </button>
      </div>

      <div style={{ display: 'flex', gap: '0.75rem', marginBottom: '1.25rem', flexWrap: 'wrap' }}>
        <input
          type="text"
          placeholder="Search username or HWID…"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          style={{
            background: 'rgba(255,255,255,0.04)',
            border: '1px solid rgba(255,255,255,0.1)',
            borderRadius: '8px',
            padding: '0.5rem 1rem',
            color: '#fff',
            fontSize: '0.875rem',
            flex: '1 1 240px',
            outline: 'none',
            fontFamily: 'inherit',
          }}
        />
        <select
          value={filter}
          onChange={(e) => setFilter(e.target.value as Filter)}
          style={{
            background: 'rgba(255,255,255,0.04)',
            border: '1px solid rgba(255,255,255,0.1)',
            borderRadius: '8px',
            padding: '0.5rem 1rem',
            color: '#fff',
            fontSize: '0.875rem',
            cursor: 'pointer',
            fontFamily: 'inherit',
          }}
        >
          <option value="all">All users</option>
          <option value="active">Active only</option>
          <option value="banned">Banned only</option>
        </select>
        <span style={{ fontSize: '0.8rem', color: '#888', alignSelf: 'center', marginLeft: 'auto' }}>
          {filtered.length} result{filtered.length !== 1 ? 's' : ''}
        </span>
      </div>

      {error && <div className={styles.errorBanner}>{error}</div>}
      {actionError && <div className={styles.errorBanner}>{actionError}</div>}

      <div className={styles.tableWrapper}>
        <table className={styles.table}>
          <thead>
            <tr>
              <th>Username</th>
              <th>HWID</th>
              <th>License</th>
              <th>Role</th>
              <th>Status</th>
              <th>Last Seen</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {filtered.length === 0 && !loading && (
              <tr>
                <td colSpan={7} className={styles.emptyCell}>
                  {search || filter !== 'all' ? 'No users match your filters.' : 'No users found.'}
                </td>
              </tr>
            )}
            {filtered.map((u) => (
              <tr key={u.id} className={u.isBanned ? styles.bannedRow : ''}>
                <td className={styles.usernameCell}>{u.username || <span style={{ color: '#555' }}>—</span>}</td>
                <td className={styles.monoCell}>
                  <span title={u.hwid}>{u.hwid.substring(0, 16)}…</span>
                  <button
                    onClick={() => copyHwid(u.hwid)}
                    title="Copy full HWID"
                    style={{
                      background: 'none',
                      border: 'none',
                      color: copied === u.hwid ? '#22cc55' : '#555',
                      cursor: 'pointer',
                      marginLeft: '0.35rem',
                      fontSize: '0.75rem',
                      padding: '0 0.25rem',
                      fontFamily: 'inherit',
                    }}
                  >
                    {copied === u.hwid ? '✓' : '⎘'}
                  </button>
                </td>
                <td className={styles.monoCell}>{maskLicense(u.licenseKey)}</td>
                <td>
                  <span style={{
                    fontSize: '0.7rem',
                    textTransform: 'uppercase',
                    letterSpacing: '0.08em',
                    color: '#888',
                    fontWeight: 600,
                  }}>
                    {(u as User & { role?: string }).role || 'user'}
                  </span>
                </td>
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
                    {pendingAction === u.hwid ? '…' : u.isBanned ? 'Unban' : 'Ban'}
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
