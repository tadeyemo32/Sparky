import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { MemoryRouter } from 'react-router-dom';
import { Login } from '../Login';
import { AuthProvider } from '../../contexts/AuthContext';
import * as authApi from '../../api/auth';

// Wrap Login in required providers
function renderLogin(initialPath = '/login') {
  return render(
    <MemoryRouter initialEntries={[initialPath]}>
      <AuthProvider>
        <Login />
      </AuthProvider>
    </MemoryRouter>
  );
}

beforeEach(() => {
  vi.restoreAllMocks();
});

describe('Login form — rendering', () => {
  it('renders username and password fields', () => {
    renderLogin();
    expect(screen.getByLabelText(/username/i)).toBeInTheDocument();
    expect(screen.getByLabelText(/password/i)).toBeInTheDocument();
  });

  it('renders the sign-in button', () => {
    renderLogin();
    expect(screen.getByRole('button', { name: /sign in/i })).toBeInTheDocument();
  });

  it('renders link to sign-up page', () => {
    renderLogin();
    expect(screen.getByRole('link', { name: /sign up/i })).toBeInTheDocument();
  });
});

describe('Login form — validation', () => {
  it('shows error when submitting empty username', async () => {
    renderLogin();
    await userEvent.click(screen.getByRole('button', { name: /sign in/i }));
    expect(await screen.findByText(/username is required/i)).toBeInTheDocument();
  });

  it('shows error when submitting empty password', async () => {
    renderLogin();
    await userEvent.type(screen.getByLabelText(/username/i), 'alice');
    await userEvent.click(screen.getByRole('button', { name: /sign in/i }));
    expect(await screen.findByText(/password is required/i)).toBeInTheDocument();
  });
});

describe('Login form — submission', () => {
  it('calls login API with trimmed username', async () => {
    const fakeSpy = vi.spyOn(authApi, 'login').mockResolvedValue({
      token: 'tok',
      username: 'alice',
      role: 'user',
      expiresAt: '9999999999',
    });
    renderLogin();
    await userEvent.type(screen.getByLabelText(/username/i), '  alice  ');
    await userEvent.type(screen.getByLabelText(/password/i), 'password123');
    await userEvent.click(screen.getByRole('button', { name: /sign in/i }));
    await waitFor(() => expect(fakeSpy).toHaveBeenCalledWith('alice', 'password123'));
  });

  it('shows loading state during submission', async () => {
    vi.spyOn(authApi, 'login').mockImplementation(
      () => new Promise((res) => setTimeout(() => res({ token: 't', username: 'u', role: 'user', expiresAt: '0' }), 100))
    );
    renderLogin();
    await userEvent.type(screen.getByLabelText(/username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/password/i), 'password123');
    fireEvent.submit(screen.getByRole('button', { name: /sign in/i }).closest('form')!);
    expect(await screen.findByText(/signing in/i)).toBeInTheDocument();
  });

  it('displays API error message on failure', async () => {
    vi.spyOn(authApi, 'login').mockRejectedValue(new Error('Invalid credentials'));
    renderLogin();
    await userEvent.type(screen.getByLabelText(/username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/password/i), 'wrongpass');
    await userEvent.click(screen.getByRole('button', { name: /sign in/i }));
    expect(await screen.findByText(/invalid credentials/i)).toBeInTheDocument();
  });

  it('disables inputs while submitting', async () => {
    vi.spyOn(authApi, 'login').mockImplementation(
      () => new Promise((res) => setTimeout(() => res({ token: 't', username: 'u', role: 'user', expiresAt: '0' }), 200))
    );
    renderLogin();
    await userEvent.type(screen.getByLabelText(/username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/password/i), 'password123');
    fireEvent.submit(screen.getByRole('button', { name: /sign in/i }).closest('form')!);
    await waitFor(() => {
      expect(screen.getByLabelText(/username/i)).toBeDisabled();
      expect(screen.getByLabelText(/password/i)).toBeDisabled();
    });
  });
});
