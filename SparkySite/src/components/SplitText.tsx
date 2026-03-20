import React from 'react';
import styles from './SplitText.module.css';

interface Props {
  text: string;
  className?: string;
  delay?: number; // ms between each char, default 40
}

export function SplitText({ text, className, delay = 40 }: Props) {
  return (
    <span className={className} aria-label={text}>
      {text.split('').map((char, i) => (
        <span
          key={i}
          aria-hidden
          className={styles.char}
          style={{ animationDelay: `${i * delay}ms` }}
        >
          {char === ' ' ? '\u00A0' : char}
        </span>
      ))}
    </span>
  );
}
