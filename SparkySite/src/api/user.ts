import { apiGetBlob } from './client';

export async function downloadLoader(token: string): Promise<Blob> {
  return apiGetBlob('/api/download', token);
}
