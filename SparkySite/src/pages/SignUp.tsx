import React, { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { signup, verifyOtp } from '../api/auth';
import { useAuth } from '../contexts/AuthContext';
import { GridCanvas } from '../components/GridCanvas';
import styles from './AuthForm.module.css';

type Step = 'register' | 'verify';

export function SignUp() {
  const { login: authLogin } = useAuth();
  const navigate = useNavigate();

  const [step, setStep] = useState<Step>('register');
  const [pendingUsername, setPendingUsername] = useState('');

  // Register step state
  const [username, setUsername] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');

  // Verify step state
  const [otp, setOtp] = useState('');

  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [cooldown, setCooldown] = useState(false);

  async function handleRegister(e: React.FormEvent) {
    e.preventDefault();
    setError(null);

    const u = username.trim();
    const em = email.trim().toLowerCase();

    if (!u) { setError('Username is required.'); return; }
    if (u.length < 3 || u.length > 32) { setError('Username must be 3–32 characters.'); return; }
    if (!/^[a-zA-Z0-9_]+$/.test(u)) { setError('Username may only contain letters, digits, and underscores.'); return; }
    if (!em || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(em)) { setError('A valid email address is required.'); return; }
    if (password.length < 8) { setError('Password must be at least 8 characters.'); return; }
    if (password !== confirmPassword) { setError('Passwords do not match.'); return; }

    setLoading(true);
    try {
      const res = await signup(u, em, password);
      setPendingUsername(res.username);
      setStep('verify');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Sign up failed. Please try again.');
      setCooldown(true);
      setTimeout(() => setCooldown(false), 2000);
    } finally {
      setLoading(false);
    }
  }

  async function handleVerify(e: React.FormEvent) {
    e.preventDefault();
    setError(null);

    if (!otp.trim() || !/^\d{6}$/.test(otp.trim())) {
      setError('Enter the 6-digit code sent to your email.');
      return;
    }

    setLoading(true);
    try {
      const response = await verifyOtp(pendingUsername, otp.trim());
      authLogin(response);
      navigate('/dashboard', { replace: true });
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Verification failed. Please try again.');
      setCooldown(true);
      setTimeout(() => setCooldown(false), 2000);
    } finally {
      setLoading(false);
    }
  }

  if (step === 'verify') {
    return (
      <div className={styles.page}>
        <div className={styles.glowBg} />
        <div className={styles.card}>
          <div className={styles.cardHeader}>
            <h1 className={styles.title}>Check your email</h1>
            <p className={styles.subtitle}>
              We sent a 6-digit code to <strong>{email}</strong>
            </p>
          </div>

          <form onSubmit={handleVerify} className={styles.form} noValidate>
            {error && <div className={styles.errorBanner}>{error}</div>}

            <div className={styles.field}>
              <label htmlFor="otp" className={styles.label}>Verification Code</label>
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

            <button type="submit" className={styles.submitBtn} disabled={loading || cooldown}>
              {loading ? 'Verifying…' : 'Verify Email'}
            </button>
          </form>

          <p className={styles.switchLink}>
            Wrong email?{' '}
            <button
              type="button"
              className={styles.link}
              style={{ background: 'none', border: 'none', cursor: 'pointer', padding: 0 }}
              onClick={() => { setStep('register'); setError(null); setOtp(''); }}
            >
              Go back
            </button>
          </p>
        </div>
      </div>
    );
  }

  return (
    <div className={styles.page}>
      <GridCanvas />
      <div className={styles.glowBg} />
      <div className={styles.card}>
        <div className={styles.cardHeader}>
          <h1 className={styles.title}>Create account</h1>
          <p className={styles.subtitle}>Choose a username and password</p>
        </div>

        <form onSubmit={handleRegister} className={styles.form} noValidate>
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

          <div className={styles.field}>
            <label htmlFor="password" className={styles.label}>Password</label>
            <input
              id="password"
              type="password"
              autoComplete="new-password"
              className={styles.input}
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="At least 8 characters"
              disabled={loading}
            />
          </div>

          <div className={styles.field}>
            <label htmlFor="confirmPassword" className={styles.label}>Confirm Password</label>
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
            {loading ? 'Creating account…' : 'Create Account'}
          </button>
        </form>

        <p className={styles.switchLink}>
          Already have an account?{' '}
          <Link to="/login" className={styles.link}>Sign in</Link>
        </p>
      </div>
    </div>
  );
}
