export const getConfig = (name) =>
  fetch(`/api/config/${name}`).then(r => {
    if (!r.ok) throw new Error(`Load failed: ${r.status}`);
    return r.json();
  });

export const saveConfig = (name, data) =>
  fetch(`/api/config/${name}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data),
  }).then(r => {
    if (!r.ok) throw new Error(`Save failed: ${r.status}`);
  });
