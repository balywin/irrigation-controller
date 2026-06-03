export function capitalize(s) {
  return s ? s[0].toUpperCase() + s.slice(1) : '';
}

// Zone groups: each group is an array of zone IDs that fire simultaneously.
// Flat arrays (legacy) are migrated to one-element groups.
export function normalizeGroups(zones) {
  if (!Array.isArray(zones) || zones.length === 0) return [];
  if (Array.isArray(zones[0])) return zones.map(g => g.map(Number));
  return zones.map(z => [Number(z)]);
}

// Toggle a day in a sorted day-of-week array. Returns new array.
export function toggleDay(days, day, checked) {
  if (checked) return [...days, day].sort((a, b) => a - b);
  return days.filter(d => d !== day);
}

// Sort start times chronologically. Returns new array.
export function sortTimes(times) {
  return [...times].sort();
}

// Add a start time (sorted). Returns new array.
export function addTime(times, defaultTime = '08:00') {
  return sortTimes([...times, defaultTime]);
}

// Remove a start time by index. Returns new array.
export function removeTime(times, index) {
  return times.filter((_, i) => i !== index);
}

// Format millisecond timestamp to locale time string.
export function fmtTime(ms) {
  if (!ms) return '';
  return new Date(ms).toLocaleTimeString();
}

// "X min" when >= 60s, "X sec" otherwise. Empty when 0/null.
export function fmtRemaining(seconds) {
  if (!seconds || seconds <= 0) return '';
  if (seconds >= 60) return `${Math.ceil(seconds / 60)} min`;
  return `${seconds} sec`;
}

// Fisher-Yates shuffle (in-place). Returns the array.
export function shuffle(arr) {
  for (let i = arr.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}
