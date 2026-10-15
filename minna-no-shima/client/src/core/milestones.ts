export interface Milestone {
  heightCm: number;
  title: string;
  /** 解放されるエリア（橋）のID。演出のみの節目は undefined */
  zone?: number;
}

export const MILESTONES: Milestone[] = [
  { heightCm: 100_00, title: "東の森への橋", zone: 1 },
  { heightCm: 500_00, title: "北の岩山への橋", zone: 2 },
  { heightCm: 1000_00, title: "西の小島への橋", zone: 3 },
  { heightCm: 2000_00, title: "夜空の花火" },
  { heightCm: 5000_00, title: "虹のアーチ" },
];

export function reachedCount(heightCm: number): number {
  return MILESTONES.filter((m) => heightCm >= m.heightCm).length;
}

export function nextMilestone(heightCm: number): Milestone | undefined {
  return MILESTONES.find((m) => heightCm < m.heightCm);
}

export function isZoneUnlocked(zone: number, heightCm: number): boolean {
  if (zone === 0) return true;
  const m = MILESTONES.find((x) => x.zone === zone);
  return !!m && heightCm >= m.heightCm;
}

export function formatMeters(cm: number): string {
  return (cm / 100).toFixed(1);
}
