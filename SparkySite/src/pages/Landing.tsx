import React from 'react';
import { Link } from 'react-router-dom';
import { ServerStatus } from '../components/ServerStatus';
import styles from './Landing.module.css';

export function Landing() {
  return (
    <div className={styles.page}>
      <div className={styles.glowBg} />

      <div className={styles.statusBar}>
        <ServerStatus />
      </div>

      <section className={styles.hero}>
        <h1 className={styles.heroTitle}>Sparky Access</h1>
        <p className={styles.heroSubtitle}>
          The world's most advanced secure deployment loader. Fully encrypted, undetectable,
          and hyper-optimized. Authenticate your HWID and securely stream your payload.
        </p>

        <div className={styles.heroCtas}>
          <Link to="/signup" className={styles.primaryBtn}>
            Get Started
          </Link>
          <Link to="/login" className={styles.secondaryBtn}>
            Sign In
          </Link>
        </div>
      </section>

      <section className={styles.features}>
        <div className={styles.featureCard}>
          <div className={styles.featureIcon}>🔒</div>
          <h3>Secure Streaming</h3>
          <p>
            Your payload is never stored on disk. It runs directly in system memory with
            military-grade SHA-256 CTR encryption.
          </p>
        </div>
        <div className={styles.featureCard}>
          <div className={styles.featureIcon}>🔑</div>
          <h3>Hardware Locked</h3>
          <p>
            Advanced cryptographic HWID locks ensure your proprietary binaries can only be
            accessed by authorized, active licenses.
          </p>
        </div>
        <div className={styles.featureCard}>
          <div className={styles.featureIcon}>☁️</div>
          <h3>Cloud Delivery</h3>
          <p>
            Global edge-network delivery via Google Cloud ensures ultra-low latency
            sub-second injection times worldwide.
          </p>
        </div>
        <div className={styles.featureCard}>
          <div className={styles.featureIcon}>⚙️</div>
          <h3>License Management</h3>
          <p>
            Tiered license system with expiry dates, HWID binding, and real-time session
            tracking for complete control.
          </p>
        </div>
        <div className={styles.featureCard}>
          <div className={styles.featureIcon}>🛡️</div>
          <h3>Undetectable</h3>
          <p>
            Engineered to bypass modern anti-cheat systems through advanced evasion
            techniques and obfuscation layers.
          </p>
        </div>
        <div className={styles.featureCard}>
          <div className={styles.featureIcon}>📊</div>
          <h3>Admin Panel</h3>
          <p>
            Full-featured administration dashboard for managing users, licenses, sessions,
            and admin roles in real time.
          </p>
        </div>
      </section>

      <footer className={styles.footer}>
        <p>© 2026 Sparky. All rights reserved.</p>
      </footer>
    </div>
  );
}
