import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { MemoryRouter } from 'react-router-dom';
import { AdminUsers } from '../Users';
import { AuthProvider } from '../../../contexts/AuthContext';
import * as adminApi from '../../../api/admin';
import type { User } from '../../../types';

// -----------------------------------------------------------------------
// Fixtures
// -----------------------------------------------------------------------
const USERS: User[] = [
  {
    id: '1',
    username: 'alice',
    hwid: 'HWID-ALICE-AAAA-BBBB',
    licenseKey: 'KEY1-ABCD-EFGH-XYZ1',
    isBanned: false,
    lastSeen: '1740000000',
  },
  {
    id: '2',
    username: 'bob',
    hwid: 'HWID-BOB-CCCC-DDDD',
    licenseKey: 'KEY2-IJKL-MNOP-XYZ2',
    isBanned: true,
    lastSeen: '1740000100',
  },
  {
    id: '3',
    username: 'charlie',
    hwid: 'HWID-CHARLIE-EEE',
    licenseKey: '',
    isBanned: false,
    lastSeen: '0',
  },
];

// Render with a logged-in admin user
function renderUsers() {
  return render(
    <MemoryRouter>
      <AuthProvider>
        <_WithAuth />
      </AuthProvider>
    </MemoryRouter>
  );
}

// Inner component that "logs in" so useAuth has a token
import React, { useEffect } from 'react';
import { useAuth } from '../../../contexts/AuthContext';

function _WithAuth() {
  const { login } = useAuth();
  useEffect(() => {
    login({ token: 'admin-token', username: 'admin', role: 'admin', expiresAt: '9999999999' });
  }, []); // eslint-disable-line react-hooks/exhaustive-deps
  return <AdminUsers />;
}

// -----------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------
beforeEach(() => {
  vi.restoreAllMocks();
  vi.spyOn(adminApi, 'getUsers').mockResolvedValue(USERS);
  vi.spyOn(adminApi, 'banUser').mockResolvedValue(undefined);
  vi.spyOn(adminApi, 'unbanUser').mockResolvedValue(undefined);
});

// -----------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------
describe('AdminUsers — rendering', () => {
  it('shows all users after load', async () => {
    renderUsers();
    expect(await screen.findByText('alice')).toBeInTheDocument();
    expect(screen.getByText('bob')).toBeInTheDocument();
    expect(screen.getByText('charlie')).toBeInTheDocument();
  });

  it('masks license key in the format KEY-****-****-LAST', async () => {
    renderUsers();
    await screen.findByText('alice');
    // alice's key: KEY1-ABCD-EFGH-XYZ1 → KEY1-****-****-XYZ1
    expect(screen.getByText('KEY1-****-****-XYZ1')).toBeInTheDocument();
  });

  it('shows — for missing license key', async () => {
    renderUsers();
    await screen.findByText('charlie');
    // charlie has empty licenseKey; should render — (there are also other — values)
    const cells = screen.getAllByText('—');
    expect(cells.length).toBeGreaterThan(0);
  });

  it('shows Active and Banned status badges', async () => {
    renderUsers();
    await screen.findByText('alice');
    const activeSpans = screen.getAllByText('Active');
    const bannedSpans = screen.getAllByText('Banned');
    expect(activeSpans.length).toBeGreaterThanOrEqual(1);
    expect(bannedSpans.length).toBeGreaterThanOrEqual(1);
  });

  it('shows Ban button for active users and Unban button for banned users', async () => {
    renderUsers();
    await screen.findByText('alice');
    const banBtns = screen.getAllByRole('button', { name: /^ban$/i });
    const unbanBtns = screen.getAllByRole('button', { name: /^unban$/i });
    expect(banBtns.length).toBeGreaterThanOrEqual(1);
    expect(unbanBtns.length).toBeGreaterThanOrEqual(1);
  });

  it('shows total user count in subtitle', async () => {
    renderUsers();
    await screen.findByText('alice');
    expect(screen.getByText(/3 total users/i)).toBeInTheDocument();
  });
});

// -----------------------------------------------------------------------
// Search
// -----------------------------------------------------------------------
describe('AdminUsers — search', () => {
  it('filters users by username', async () => {
    renderUsers();
    await screen.findByText('alice');

    await userEvent.type(screen.getByPlaceholderText(/search/i), 'ali');
    expect(screen.getByText('alice')).toBeInTheDocument();
    expect(screen.queryByText('bob')).not.toBeInTheDocument();
    expect(screen.queryByText('charlie')).not.toBeInTheDocument();
  });

  it('filters users by HWID prefix', async () => {
    renderUsers();
    await screen.findByText('alice');

    await userEvent.type(screen.getByPlaceholderText(/search/i), 'HWID-BOB');
    expect(screen.queryByText('alice')).not.toBeInTheDocument();
    expect(screen.getByText('bob')).toBeInTheDocument();
  });

  it('shows empty state message when no results', async () => {
    renderUsers();
    await screen.findByText('alice');

    await userEvent.type(screen.getByPlaceholderText(/search/i), 'nonexistent-xyz');
    expect(screen.getByText(/no users match your filters/i)).toBeInTheDocument();
  });

  it('updates result count text', async () => {
    renderUsers();
    await screen.findByText('alice');
    expect(screen.getByText(/3 results/i)).toBeInTheDocument();

    await userEvent.type(screen.getByPlaceholderText(/search/i), 'ali');
    expect(screen.getByText(/1 result/i)).toBeInTheDocument();
  });
});

// -----------------------------------------------------------------------
// Filter dropdown
// -----------------------------------------------------------------------
describe('AdminUsers — filter dropdown', () => {
  it('shows only active users when Active only selected', async () => {
    renderUsers();
    await screen.findByText('alice');

    await userEvent.selectOptions(screen.getByRole('combobox'), 'active');
    expect(screen.getByText('alice')).toBeInTheDocument();
    expect(screen.getByText('charlie')).toBeInTheDocument();
    expect(screen.queryByText('bob')).not.toBeInTheDocument();
  });

  it('shows only banned users when Banned only selected', async () => {
    renderUsers();
    await screen.findByText('alice');

    await userEvent.selectOptions(screen.getByRole('combobox'), 'banned');
    expect(screen.getByText('bob')).toBeInTheDocument();
    expect(screen.queryByText('alice')).not.toBeInTheDocument();
    expect(screen.queryByText('charlie')).not.toBeInTheDocument();
  });
});

// -----------------------------------------------------------------------
// Ban / Unban actions
// -----------------------------------------------------------------------
describe('AdminUsers — ban/unban', () => {
  it('calls banUser with correct hwid when Ban clicked', async () => {
    renderUsers();
    await screen.findByText('alice');

    // alice is active → has a Ban button
    // find the row containing "alice" and click its Ban button
    const row = screen.getByText('alice').closest('tr')!;
    await userEvent.click(within(row).getByRole('button', { name: /^ban$/i }));

    await waitFor(() =>
      expect(adminApi.banUser).toHaveBeenCalledWith('admin-token', USERS[0].hwid)
    );
  });

  it('calls unbanUser with correct hwid when Unban clicked', async () => {
    renderUsers();
    await screen.findByText('bob');

    const row = screen.getByText('bob').closest('tr')!;
    await userEvent.click(within(row).getByRole('button', { name: /^unban$/i }));

    await waitFor(() =>
      expect(adminApi.unbanUser).toHaveBeenCalledWith('admin-token', USERS[1].hwid)
    );
  });

  it('reloads user list after ban action', async () => {
    renderUsers();
    await screen.findByText('alice');

    const row = screen.getByText('alice').closest('tr')!;
    await userEvent.click(within(row).getByRole('button', { name: /^ban$/i }));

    await waitFor(() => expect(adminApi.getUsers).toHaveBeenCalledTimes(2));
  });

  it('shows action error when ban fails', async () => {
    vi.spyOn(adminApi, 'banUser').mockRejectedValue(new Error('ban failed'));
    renderUsers();
    await screen.findByText('alice');

    const row = screen.getByText('alice').closest('tr')!;
    await userEvent.click(within(row).getByRole('button', { name: /^ban$/i }));

    expect(await screen.findByText(/ban failed/i)).toBeInTheDocument();
  });
});
