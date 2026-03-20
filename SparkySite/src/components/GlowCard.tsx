import React, { useRef } from 'react';
import styles from './GlowCard.module.css';

interface Props {
  children: React.ReactNode;
  innerClassName?: string;
}

export function GlowCard({ children, innerClassName }: Props) {
  const ref = useRef<HTMLDivElement>(null);

  function onMove(e: React.MouseEvent<HTMLDivElement>) {
    if (!ref.current) return;
    const r = ref.current.getBoundingClientRect();
    ref.current.style.setProperty('--gx', `${e.clientX - r.left}px`);
    ref.current.style.setProperty('--gy', `${e.clientY - r.top}px`);
  }

  function onLeave() {
    if (!ref.current) return;
    // move gradient off-card so it fades out
    ref.current.style.setProperty('--gx', '-200px');
    ref.current.style.setProperty('--gy', '-200px');
  }

  return (
    <div
      ref={ref}
      className={styles.wrapper}
      onMouseMove={onMove}
      onMouseLeave={onLeave}
    >
      <div className={`${styles.inner}${innerClassName ? ` ${innerClassName}` : ''}`}>
        {children}
      </div>
    </div>
  );
}
