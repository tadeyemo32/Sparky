import React from 'react';
import { Routes, Route, Navigate } from 'react-router-dom';
import { Navbar } from './components/Navbar';
import { ProtectedRoute } from './components/ProtectedRoute';
import { AdminRoute } from './components/AdminRoute';
import { OwnerRoute } from './components/OwnerRoute';

import { Landing } from './pages/Landing';
import { Login } from './pages/Login';
import { SignUp } from './pages/SignUp';
import { Dashboard } from './pages/Dashboard';
import { Download } from './pages/Download';
import { AdminUsers } from './pages/admin/Users';
import { AdminLicenses } from './pages/admin/Licenses';
import { AdminSessions } from './pages/admin/Sessions';
import { OwnerAdmins } from './pages/admin/Admins';
import { OwnerMetrics } from './pages/owner/Metrics';

export default function App() {
  return (
    <>
      <Navbar />

      <main style={{ flex: 1 }}>
        <Routes>
          {/* Public routes */}
          <Route path="/" element={<Landing />} />
          <Route path="/login" element={<Login />} />
          <Route path="/signup" element={<SignUp />} />

          {/* User routes */}
          <Route
            path="/dashboard"
            element={
              <ProtectedRoute>
                <Dashboard />
              </ProtectedRoute>
            }
          />
          <Route
            path="/download"
            element={
              <ProtectedRoute>
                <Download />
              </ProtectedRoute>
            }
          />

          {/* Admin routes */}
          <Route
            path="/admin/users"
            element={
              <AdminRoute>
                <AdminUsers />
              </AdminRoute>
            }
          />
          <Route
            path="/admin/licenses"
            element={
              <AdminRoute>
                <AdminLicenses />
              </AdminRoute>
            }
          />
          <Route
            path="/admin/sessions"
            element={
              <AdminRoute>
                <AdminSessions />
              </AdminRoute>
            }
          />

          {/* Owner routes */}
          <Route
            path="/owner/admins"
            element={
              <OwnerRoute>
                <OwnerAdmins />
              </OwnerRoute>
            }
          />
          <Route
            path="/owner/metrics"
            element={
              <OwnerRoute>
                <OwnerMetrics />
              </OwnerRoute>
            }
          />

          {/* Catch-all */}
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </main>
    </>
  );
}
