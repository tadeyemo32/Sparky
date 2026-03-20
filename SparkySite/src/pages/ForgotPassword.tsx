import React, { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { forgotPassword, resetPassword } from '../api/auth';
import { GridCanvas } from '../components/GridCanvas';
import styles from './AuthForm.module.css';

type Step = 'email' | 'reset' | 'done';

export function ForgotPassword() {
  const navigate = useNavigate();
  const [step, setStep] = useState<Step>('email');

  const [email, setEmail] = useState('');
  const [username, setUsername] = useState('');
  const [otp, setOtp] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');

  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [cooldown, setCooldown] = useState(false);

  async function handleRequestCode(e: React.FormEvent) {
    e.preventDefault();
    setError(null);

    const em = email.trim().toLowerCase();
    if (!em || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(em)) {
      setError('Enter a valid email address.');
      return;
    }

    setLoading(true);
    try {
      await forgotPassword(em);
      setStep('reset');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Something went wrong. Please try again.');
      setCooldown(true);
      setTimeout(() => setCooldown(false), 2000);
    } finally {
      setLoading(false);
    }
  }

  async function handleReset(e: React.FormEvent) {
    e.preventDefault();
    setError(null);

    const u = username.trim();
    if (!u) { setError('Username is required.'); return; }
    if (!otp.trim() || !/^\d{6}$/.test(otp.trim())) {
      setError('Enter the 6-digit code from your email.');
      return;
    }
    if (newPassword.length < 8) { setError('Password must be at least 8 characters.'); return; }
    if (newPassword !== confirmPassword) { setError('Passwords do not match.'); return; }

    setLoading(true);
    try {
      await resetPassword(u, otp.trim(), newPassword);
      setStep('done');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Reset failed. Please try again.');
      setCooldown(true);
      setTimeout(() => setCooldown(false), 2000);
    } finally {
      setLoading(false);
    }
  }

  if (step === 'done') {
    return (
      <div className={styles.page}>
        <GridCanvas />
        <div className={styles.glowBg} />
        <div className={styles.card}>
          <div className={styles.cardHeader}>
            <h1 className={styles.title}>Password updated</h1>
            <p className={styles.subtitle}>Your password has been reset successfully.</p>
          </div>
          <button
            type="button"
            className={styles.submitBtn}
            onClick={() => navigate('/login', { replace: true })}
          >
            Sign In
          </button>
        </div>
      </div>
    );
  }

  if (step === 'reset') {
    return (
      <div className={styles.page}>
        <GridCanvas />
        <div className={styles.glowBg} />
        <div className={styles.card}>
          <div className={styles.cardHeader}>
            <h1 className={styles.title}>Reset password</h1>
            <p className={styles.subtitle}>
              Enter the code sent to <strong>{email}</strong> and your new password.
            </p>
          </div>

          <form onSubmit={handleReset} className={styles.form} noValidate>
            {error && <div className={styles.errorBanner}>{error}</div>}

            <div className={styles.field}>
              <label htmlFor="username" className={styles.label}>Username</label>
              <input
                id="username"
                type="text"
                autoComplete="username"
                className={styles.input}
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                placeholder="your_username"
                disabled={loading}
              />
            </div>

            <div className={styles.field}>
              <label htmlFor="otp" className={styles.label}>Reset Code</label>
              <input
                id="otp"
                type="text"
                inputMode="numeric"
                pattern="[0-9]{6}"
                maxLength={6}
                autoComplete="one-time-code"
                className={styles.input}
                value={otp}
                onChange={(e) => setOtp(e.target.value.replace(/\D/g, ''))}
                placeholder="000000"
                disabled={loading}
              />
            </div>

            <div className={styles.field}>
              <label htmlFor="newPassword" className={styles.label}>New Password</label>
              <input
                id="newPassword"
                type="password"
                autoComplete="new-password"
                className={styles.input}
                value={newPassword}
                onChange={(e) => setNewPassword(e.target.value)}
                placeholder="At least 8 characters"
                disabled={loading}
              />
            </div>

            <div className={styles.field}>
              <label htmlFor="confirmPassword" className={styles.label}>Confirm New Password</label>
              <input
                id="confirmPassword"
                type="password"
                autoComplete="new-password"
                className={styles.input}
                value={confirmPassword}
                onChange={(e) => setConfirmPassword(e.target.value)}
                placeholder="Repeat password"
                disabled={loading}
              />
            </div>

            <button type="submit" className={styles.submitBtn} disabled={loading || cooldown}>
              {loading ? 'Updating…' : 'Set New Password'}
            </button>
          </form>

          <p className={styles.switchLink}>
            Didn't receive a code?{' '}
            <button
              type="button"
              className={styles.link}
              style={{ background: 'none', border: 'none', cursor: 'pointer', padding: 0 }}
              onClick={() => { setStep('email'); setError(null); }}
            >
              Try again
            </button>
          </p>
        </div>
      </div>
    );
  }

  return (
    <div className={styles.page}>
      <div className={styles.glowBg} />
      <div className={styles.card}>
        <div className={styles.cardHeader}>
          <h1 className={styles.title}>Forgot password?</h1>
          <p className={styles.subtitle}>
            Enter your registered email and we'll send you a reset code.
          </p>
        </div>

        <form onSubmit={handleRequestCode} className={styles.form} noValidate>
          {error && <div className={styles.errorBanner}>{error}</div>}

          <div className={styles.field}>
            <label htmlFor="email" className={styles.label}>Email</label>
            <input
              id="email"
              type="email"
              autoComplete="email"
              className={styles.input}
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              placeholder="you@example.com"
              disabled={loading}
            />
          </div>

          <button type="submit" className={styles.submitBtn} disabled={loading || cooldown}>
            {loading ? 'Sending…' : 'Send Reset Code'}
          </button>
        </form>

        <p className={styles.switchLink}>
          Remember it?{' '}
          <Link to="/login" className={styles.link}>Sign in</Link>
        </p>
      </div>
    </div>
  );
}
