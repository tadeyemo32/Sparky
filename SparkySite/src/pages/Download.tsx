import React, { useState, useEffect, useRef } from 'react';
import { downloadLoader } from '../api/user';
import { useAuth } from '../contexts/AuthContext';
import styles from './Download.module.css';

function randomFilename(): string {
  const adjectives = ['secure', 'crypto', 'stream', 'ghost', 'phantom', 'stealth', 'shadow'];
  const nouns = ['loader', 'client', 'runner', 'agent', 'core', 'bridge', 'link'];
  const adj = adjectives[Math.floor(Math.random() * adjectives.length)];
  const noun = nouns[Math.floor(Math.random() * nouns.length)];
  const suffix = Math.floor(Math.random() * 9000 + 1000);
  return `${adj}_${noun}_${suffix}.exe`;
}

export function Download() {
  const { user } = useAuth();
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [success, setSuccess] = useState(false);
  const abortRef = useRef<AbortController | null>(null);

  useEffect(() => {
    return () => {
      abortRef.current?.abort();
    };
  }, []);

  async function handleDownload() {
    if (!user?.token) return;

    abortRef.current?.abort();
    const controller = new AbortController();
    abortRef.current = controller;

    setError(null);
    setSuccess(false);
    setLoading(true);

    try {
      const blob = await downloadLoader(user.token, controller.signal);
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = randomFilename();
      document.body.appendChild(anchor);
      anchor.click();
      anchor.remove();
      URL.revokeObjectURL(url);
      setSuccess(true);
    } catch (err) {
      if (err instanceof Error) {
        setError(err.message);
      } else {
        setError('Download failed. Please try again.');
      }
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className={styles.page}>
      <div className={styles.glowBg} />

      <div className={styles.content}>
        <h1 className={styles.title}>Download Loader</h1>
        <p className={styles.subtitle}>
          Securely download your authenticated SparkyLoader client. The binary is personalized
          to your session and HWID.
        </p>

        <div className={styles.card}>
          <div className={styles.cardIcon}>⚡</div>
          <h2 className={styles.cardTitle}>SparkyLoader.exe</h2>
          <p className={styles.cardDesc}>
            The Sparky loader authenticates with the server using your HWID, verifies your license,
            and streams your payload directly into memory. No files are written to disk.
          </p>

          {error && <div className={styles.errorBanner}>{error}</div>}
          {success && (
            <div className={styles.successBanner}>
              Download started! Your loader has been saved with a randomised filename.
            </div>
          )}

          <button
            onClick={handleDownload}
            className={styles.downloadBtn}
            disabled={loading}
          >
            {loading ? (
              <>
                <span className={styles.spinner} />
                Downloading…
              </>
            ) : (
              <>
                <svg
                  width="20"
                  height="20"
                  viewBox="0 0 24 24"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="2"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                  aria-hidden="true"
                >
                  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
                  <polyline points="7 10 12 15 17 10" />
                  <line x1="12" y1="15" x2="12" y2="3" />
                </svg>
                Download SparkyLoader
              </>
            )}
          </button>
        </div>

        <div className={styles.warnings}>
          <div className={styles.warningItem}>
            <span className={styles.warningIcon}>🔒</span>
            <span>Your download is authenticated — only valid sessions can access the binary.</span>
          </div>
          <div className={styles.warningItem}>
            <span className={styles.warningIcon}>🛡️</span>
            <span>The filename is randomised on each download to aid in evasion.</span>
          </div>
          <div className={styles.warningItem}>
            <span className={styles.warningIcon}>⚠️</span>
            <span>Only run the loader on your HWID-registered device.</span>
          </div>
        </div>
      </div>
    </div>
  );
}
