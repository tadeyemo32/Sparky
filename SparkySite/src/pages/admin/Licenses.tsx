import React, { useCallback, useEffect, useState } from 'react';
import { getLicenses, issueLicense } from '../../api/admin';
import { useAuth } from '../../contexts/AuthContext';
import type { License } from '../../types';
import styles from './Admin.module.css';

export function AdminLicenses() {
  const { user } = useAuth();
  const [licenses, setLicenses] = useState<License[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Modal state
  const [modalOpen, setModalOpen] = useState(false);
  const [issueTier, setIssueTier] = useState<number>(1);
  const [issueDays, setIssueDays] = useState<number>(30);
  const [issueLoading, setIssueLoading] = useState(false);
  const [issueError, setIssueError] = useState<string | null>(null);
  const [issuedKey, setIssuedKey] = useState<string | null>(null);
  const [copySuccess, setCopySuccess] = useState(false);

  const load = useCallback(async (signal?: AbortSignal) => {
    if (!user?.token) return;
    setError(null);
    try {
      const data = await getLicenses(user.token, signal);
      setLicenses(data);
    } catch (err) {
      if (err instanceof Error && err.name !== 'AbortError')
        setError(err.message || 'Failed to load licenses.');
    } finally {
      setLoading(false);
    }
  }, [user?.token]);

  useEffect(() => {
    const controller = new AbortController();
    load(controller.signal);
    return () => controller.abort();
  }, [load]);

  function formatDate(iso: string) {
    if (!iso) return 'Never';
    try {
      return new Date(iso).toLocaleDateString(undefined, {
        year: 'numeric',
        month: 'short',
        day: 'numeric',
      });
    } catch {
      return iso;
    }
  }

  function openModal() {
    setModalOpen(true);
    setIssuedKey(null);
    setIssueError(null);
    setCopySuccess(false);
    setIssueTier(1);
    setIssueDays(30);
  }

  function closeModal() {
    setModalOpen(false);
    setIssuedKey(null);
    setIssueError(null);
  }

  async function handleIssue(e: React.FormEvent) {
    e.preventDefault();
    if (!user?.token) return;
    setIssueLoading(true);
    setIssueError(null);
    setIssuedKey(null);
    try {
      const result = await issueLicense(user.token, issueTier, issueDays);
      setIssuedKey(result.key);
      await load();
    } catch (err) {
      setIssueError(err instanceof Error ? err.message : 'Failed to issue license.');
    } finally {
      setIssueLoading(false);
    }
  }

  async function handleCopy() {
    if (!issuedKey) return;
    try {
      await navigator.clipboard.writeText(issuedKey);
      setCopySuccess(true);
      setTimeout(() => setCopySuccess(false), 2000);
    } catch {
      // clipboard API may not be available
    }
  }

  return (
    <div className={styles.page}>
      <div className={styles.header}>
        <div>
          <h1 className={styles.title}>License Management</h1>
          <p className={styles.subtitle}>View all licenses and issue new ones.</p>
        </div>
        <div className={styles.headerActions}>
          <button onClick={() => load()} className={styles.refreshBtn} disabled={loading}>
            {loading ? 'Loading…' : 'Refresh'}
          </button>
          <button onClick={openModal} className={styles.primaryBtn}>
            Issue License
          </button>
        </div>
      </div>

      {error && <div className={styles.errorBanner}>{error}</div>}

      <div className={styles.tableWrapper}>
        <table className={styles.table}>
          <thead>
            <tr>
              <th>Key (masked)</th>
              <th>Tier</th>
              <th>HWID (masked)</th>
              <th>Expires</th>
              <th>Bound</th>
            </tr>
          </thead>
          <tbody>
            {licenses.length === 0 && !loading && (
              <tr>
                <td colSpan={5} className={styles.emptyCell}>
                  No licenses found.
                </td>
              </tr>
            )}
            {licenses.map((lic, idx) => (
              <tr key={idx}>
                <td className={styles.monoCell}>{lic.key}</td>
                <td>
                  <span className={styles.tierBadge}>Tier {lic.tier}</span>
                </td>
                <td className={styles.monoCell}>{lic.hwid || '—'}</td>
                <td className={styles.dateCell}>{formatDate(lic.expiresAt)}</td>
                <td>
                  <span className={lic.isBound ? styles.boundBadge : styles.freeBadge}>
                    {lic.isBound ? 'Bound' : 'Free'}
                  </span>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {modalOpen && (
        <div className={styles.modalOverlay} onClick={closeModal}>
          <div
            className={styles.modal}
            onClick={(e) => e.stopPropagation()}
            role="dialog"
            aria-modal="true"
            aria-labelledby="modal-title"
          >
            <div className={styles.modalHeader}>
              <h2 id="modal-title" className={styles.modalTitle}>
                Issue New License
              </h2>
              <button onClick={closeModal} className={styles.modalClose} aria-label="Close">
                ×
              </button>
            </div>

            {!issuedKey ? (
              <form onSubmit={handleIssue} className={styles.modalForm}>
                {issueError && <div className={styles.errorBanner}>{issueError}</div>}

                <div className={styles.field}>
                  <label htmlFor="tier" className={styles.label}>
                    Tier
                  </label>
                  <select
                    id="tier"
                    className={styles.select}
                    value={issueTier}
                    onChange={(e) => setIssueTier(Number(e.target.value))}
                    disabled={issueLoading}
                  >
                    <option value={1}>Tier 1</option>
                    <option value={2}>Tier 2</option>
                    <option value={3}>Tier 3</option>
                    <option value={4}>Tier 4</option>
                  </select>
                </div>

                <div className={styles.field}>
                  <label htmlFor="days" className={styles.label}>
                    Days (0 = lifetime)
                  </label>
                  <input
                    id="days"
                    type="number"
                    min={0}
                    className={styles.input}
                    value={issueDays}
                    onChange={(e) => setIssueDays(Number(e.target.value))}
                    disabled={issueLoading}
                  />
                </div>

                <button type="submit" className={styles.submitBtn} disabled={issueLoading}>
                  {issueLoading ? 'Issuing…' : 'Issue License'}
                </button>
              </form>
            ) : (
              <div className={styles.issuedResult}>
                <p className={styles.issuedLabel}>New license key:</p>
                <div className={styles.keyDisplay}>
                  <code className={styles.keyCode}>{issuedKey}</code>
                </div>
                <button onClick={handleCopy} className={styles.copyBtn}>
                  {copySuccess ? 'Copied!' : 'Copy to Clipboard'}
                </button>
                <button onClick={closeModal} className={styles.doneBtn}>
                  Done
                </button>
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
