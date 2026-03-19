import React from 'react';
import { useServerStatus } from '../hooks/useServerStatus';
import styles from './ServerStatus.module.css';

export function ServerStatus() {
  const status = useServerStatus();

  const label =
    status === 'checking' ? 'Checking...' : status === 'online' ? 'Online' : 'Offline';

  return (
    <span className={styles.wrapper}>
      <span className={`${styles.dot} ${styles[status]}`} />
      <span className={styles.label}>{label}</span>
    </span>
  );
}
