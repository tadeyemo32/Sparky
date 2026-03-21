import { apiFetch } from './client';

export async function downloadLoader(token: string, signal?: AbortSignal): Promise<Blob> {
  return apiFetch<Blob>('/api/download', { method: 'GET', token, responseType: 'blob', signal });
}
