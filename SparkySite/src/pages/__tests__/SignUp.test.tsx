import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { MemoryRouter } from 'react-router-dom';
import { SignUp } from '../SignUp';
import { AuthProvider } from '../../contexts/AuthContext';
import * as authApi from '../../api/auth';

function renderSignUp() {
  return render(
    <MemoryRouter initialEntries={['/signup']}>
      <AuthProvider>
        <SignUp />
      </AuthProvider>
    </MemoryRouter>
  );
}

beforeEach(() => {
  vi.restoreAllMocks();
});

describe('SignUp form — rendering', () => {
  it('renders username, password, and confirm password fields', () => {
    renderSignUp();
    expect(screen.getByLabelText(/^username/i)).toBeInTheDocument();
    expect(screen.getByLabelText(/^password$/i)).toBeInTheDocument();
    expect(screen.getByLabelText(/confirm password/i)).toBeInTheDocument();
  });

  it('does NOT render a license key field', () => {
    renderSignUp();
    expect(screen.queryByLabelText(/license/i)).not.toBeInTheDocument();
    expect(screen.queryByPlaceholderText(/license/i)).not.toBeInTheDocument();
  });

  it('renders the create account button', () => {
    renderSignUp();
    expect(screen.getByRole('button', { name: /create account/i })).toBeInTheDocument();
  });
});

describe('SignUp form — validation', () => {
  it('requires username', async () => {
    renderSignUp();
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));
    expect(await screen.findByText(/username is required/i)).toBeInTheDocument();
  });

  it('rejects username shorter than 3 characters', async () => {
    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'ab');
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));
    expect(await screen.findByText(/3.{1,10}32 char/i)).toBeInTheDocument();
  });

  it('rejects username longer than 32 characters', async () => {
    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'a'.repeat(33));
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));
    expect(await screen.findByText(/3.{1,10}32 char/i)).toBeInTheDocument();
  });

  it('rejects username with special characters', async () => {
    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'alice!');
    await userEvent.type(screen.getByLabelText(/^password$/i), 'password123');
    await userEvent.type(screen.getByLabelText(/confirm password/i), 'password123');
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));
    expect(await screen.findByText(/letters, digits.*underscore/i)).toBeInTheDocument();
  });

  it('rejects password shorter than 8 characters', async () => {
    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/^password$/i), 'short');
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));
    expect(await screen.findByText(/at least 8 char/i)).toBeInTheDocument();
  });

  it('rejects mismatched passwords', async () => {
    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/^password$/i), 'password123');
    await userEvent.type(screen.getByLabelText(/confirm password/i), 'differentpass');
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));
    expect(await screen.findByText(/passwords do not match/i)).toBeInTheDocument();
  });
});

describe('SignUp form — submission', () => {
  it('calls signup without licenseKey in the payload', async () => {
    const spy = vi.spyOn(authApi, 'signup').mockResolvedValue({
      token: 'tok',
      username: 'alice',
      role: 'user',
      expiresAt: '9999999999',
    });

    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/^password$/i), 'password123');
    await userEvent.type(screen.getByLabelText(/confirm password/i), 'password123');
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));

    await waitFor(() => expect(spy).toHaveBeenCalledOnce());
    // Second arg is the licenseKey passed through (should be empty string)
    expect(spy.mock.calls[0][1]).toBe('');
  });

  it('shows API error on failure', async () => {
    vi.spyOn(authApi, 'signup').mockRejectedValue(new Error('username already taken'));

    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/^password$/i), 'password123');
    await userEvent.type(screen.getByLabelText(/confirm password/i), 'password123');
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));

    expect(await screen.findByText(/username already taken/i)).toBeInTheDocument();
  });

  it('shows loading state while submitting', async () => {
    vi.spyOn(authApi, 'signup').mockImplementation(
      () => new Promise((res) => setTimeout(() => res({ token: 't', username: 'u', role: 'user', expiresAt: '0' }), 100))
    );

    renderSignUp();
    await userEvent.type(screen.getByLabelText(/^username/i), 'alice');
    await userEvent.type(screen.getByLabelText(/^password$/i), 'password123');
    await userEvent.type(screen.getByLabelText(/confirm password/i), 'password123');
    await userEvent.click(screen.getByRole('button', { name: /create account/i }));

    expect(await screen.findByText(/creating account/i)).toBeInTheDocument();
  });
});
