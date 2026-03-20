import React from 'react';
import { Link } from 'react-router-dom';
import { GridCanvas } from '../components/GridCanvas';
import { SplitText } from '../components/SplitText';
import { GlowCard } from '../components/GlowCard';
import styles from './Landing.module.css';

export function Landing() {
  return (
    <>
      <GridCanvas />

      <div className={styles.page}>
        <section className={styles.hero}>
          <p className={styles.heroEyebrow}>Secure Deployment System</p>
          <h1 className={styles.heroTitle}>
            <SplitText text="Sparky Access" />
          </h1>
          <p className={styles.heroSubtitle}>
            The world's most advanced secure deployment loader. Fully encrypted, undetectable,
            and hyper-optimized. Authenticate your HWID and securely stream your payload.
          </p>
          <div className={styles.heroCtas}>
            <Link to="/signup" className={styles.primaryBtn}>Get Started</Link>
            <Link to="/login" className={styles.secondaryBtn}>Sign In</Link>
          </div>
        </section>

        <div className={styles.cardsSection}>
          <div className={styles.cardsGrid}>
            <GlowCard innerClassName={styles.card}>
              <span className={styles.cardIcon}>🔒</span>
              <h3>Secure Streaming</h3>
              <p>Your payload is never stored on disk. It runs directly in system memory with military-grade SHA-256 CTR encryption.</p>
            </GlowCard>
            <GlowCard innerClassName={styles.card}>
              <span className={styles.cardIcon}>🔑</span>
              <h3>Hardware Locked</h3>
              <p>Advanced cryptographic HWID locks ensure your proprietary binaries can only be accessed by authorized, active licenses.</p>
            </GlowCard>
            <GlowCard innerClassName={styles.card}>
              <span className={styles.cardIcon}>☁️</span>
              <h3>Cloud Delivery</h3>
              <p>Global edge-network delivery via Google Cloud ensures ultra-low latency sub-second injection times worldwide.</p>
            </GlowCard>
            <GlowCard innerClassName={styles.card}>
              <span className={styles.cardIcon}>⚙️</span>
              <h3>License Management</h3>
              <p>Tiered license system with expiry dates, HWID binding, and instant revocation from the admin panel.</p>
            </GlowCard>
            <GlowCard innerClassName={styles.card}>
              <span className={styles.cardIcon}>🛡️</span>
              <h3>Undetectable</h3>
              <p>Engineered to bypass modern anti-cheat systems using advanced memory-mapping and obfuscation techniques.</p>
            </GlowCard>
            <GlowCard innerClassName={styles.card}>
              <span className={styles.cardIcon}>📊</span>
              <h3>Admin Panel</h3>
              <p>Full-featured administration dashboard for managing users, licenses, and real-time deployment analytics.</p>
            </GlowCard>
          </div>
        </div>
      </div>
    </>
  );
}
