import React from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { ServerStatus } from './ServerStatus';
import styles from './Navbar.module.css';

function SparkyStar() {
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">
      <path d="M8 0L10 6H16L11 9.5L13 16L8 12L3 16L5 9.5L0 6H6L8 0Z" />
    </svg>
  );
}

export function Navbar() {
  const { user, isAuthenticated, isAdmin, isOwner, logout } = useAuth();
  const navigate = useNavigate();

  async function handleLogout() {
    await logout();
    navigate('/');
  }

  return (
    <nav className={styles.nav}>
      <div className={styles.left}>
        <Link to="/" className={styles.logo}>
          <SparkyStar />
          SPARKY
        </Link>

        {isAuthenticated && (
          <div className={styles.links}>
            <Link to="/dashboard" className={styles.link}>Dashboard</Link>
            <Link to="/download" className={styles.link}>Download</Link>
            {isAdmin && (
              <>
                <Link to="/admin/users" className={styles.link}>Users</Link>
                <Link to="/admin/licenses" className={styles.link}>Licenses</Link>
                <Link to="/admin/sessions" className={styles.link}>Sessions</Link>
              </>
            )}
            {isOwner && (
              <>
                <Link to="/owner/admins" className={styles.link}>Admins</Link>
                <Link to="/owner/metrics" className={styles.link}>Metrics</Link>
              </>
            )}
          </div>
        )}
      </div>

      <div className={styles.right}>
        <ServerStatus />

        {isAuthenticated && user ? (
          <>
            <span className={styles.userPill}>
              {user.username}
              <span className={styles.roleBadge}>{user.role}</span>
            </span>
            <button onClick={handleLogout} className={styles.logoutBtn}>
              Logout
            </button>
          </>
        ) : (
          <>
            <Link to="/login" className={styles.loginLink}>Login</Link>
            <Link to="/signup" className={styles.signupBtn}>Sign Up</Link>
          </>
        )}
      </div>
    </nav>
  );
}
