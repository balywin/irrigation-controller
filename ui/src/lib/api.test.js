import { describe, it, expect, vi, beforeEach } from 'vitest';
import { getConfig, saveConfig } from './api.js';

const mockFetch = vi.fn();
global.fetch = mockFetch;

describe('getConfig', () => {
  beforeEach(() => mockFetch.mockClear());

  it('GETs /api/config/<name> and returns parsed JSON', async () => {
    mockFetch.mockResolvedValue({ ok: true, json: () => Promise.resolve({ foo: 'bar' }) });
    const result = await getConfig('app_config.json');
    expect(mockFetch).toHaveBeenCalledWith('/api/config/app_config.json');
    expect(result).toEqual({ foo: 'bar' });
  });

  it('throws on network failure', async () => {
    mockFetch.mockRejectedValueOnce(new Error('Network error'));
    await expect(getConfig('app_config.json')).rejects.toThrow('Network error');
  });

  it('throws when response is not ok', async () => {
    mockFetch.mockResolvedValueOnce({ ok: false, status: 404 });
    await expect(getConfig('app_config.json')).rejects.toThrow('Load failed: 404');
  });
});

describe('saveConfig', () => {
  beforeEach(() => mockFetch.mockClear());

  it('POSTs JSON body to /api/config/<name>', async () => {
    mockFetch.mockResolvedValue({ ok: true });
    await saveConfig('app_config.json', { foo: 'bar' });
    expect(mockFetch).toHaveBeenCalledWith('/api/config/app_config.json', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ foo: 'bar' }),
    });
  });

  it('throws on network failure', async () => {
    mockFetch.mockRejectedValueOnce(new Error('Network error'));
    await expect(saveConfig('app_config.json', {})).rejects.toThrow('Network error');
  });

  it('throws when response.ok is false', async () => {
    mockFetch.mockResolvedValue({ ok: false, status: 500 });
    await expect(saveConfig('app_config.json', {})).rejects.toThrow('Save failed: 500');
  });
});
