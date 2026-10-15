export const STAMPS = ["👋", "👍", "😂", "🎉", "❤️", "😮", "🙏", "✨"] as const;
export type StampId = number; // STAMPS のインデックス

export function isValidStamp(id: unknown): id is StampId {
  return typeof id === "number" && Number.isInteger(id) && id >= 0 && id < STAMPS.length;
}
