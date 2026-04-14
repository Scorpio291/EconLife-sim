import { useSimStore } from '../store';

function formatMoney(n: number): string {
  if (Math.abs(n) >= 1_000_000) return `$${(n / 1_000_000).toFixed(2)}M`;
  if (Math.abs(n) >= 1_000) return `$${(n / 1_000).toFixed(1)}K`;
  return `$${n.toFixed(0)}`;
}

export function MetricsDashboard() {
  const state = useSimStore((s) => s.state);

  if (!state) return null;

  const { metrics, provinces } = state;

  return (
    <div className="metrics-dashboard">
      <h2>Economy Overview</h2>

      <div className="metrics-grid">
        <div className="metric-card">
          <span className="metric-label">Avg NPC Capital</span>
          <span className="metric-value">{formatMoney(metrics.avg_npc_capital)}</span>
        </div>
        <div className="metric-card">
          <span className="metric-label">Avg Spot Price</span>
          <span className="metric-value">{formatMoney(metrics.avg_spot_price)}</span>
        </div>
        <div className="metric-card">
          <span className="metric-label">NPCs</span>
          <span className="metric-value">{metrics.npc_count.toLocaleString()}</span>
        </div>
        <div className="metric-card">
          <span className="metric-label">Businesses</span>
          <span className="metric-value">{metrics.business_count.toLocaleString()}</span>
        </div>
      </div>

      <h3>Province Conditions</h3>
      <table className="province-table">
        <thead>
          <tr>
            <th>Province</th>
            <th>Population</th>
            <th>Stability</th>
            <th>Crime</th>
            <th>Grievance</th>
            <th>Cohesion</th>
          </tr>
        </thead>
        <tbody>
          {provinces.map((p) => (
            <tr key={p.id}>
              <td>{p.name}</td>
              <td>{(p.population / 1000).toFixed(0)}K</td>
              <td className={p.stability < 0.5 ? 'warn' : ''}>{(p.stability * 100).toFixed(0)}%</td>
              <td className={p.crime > 0.15 ? 'warn' : ''}>{(p.crime * 100).toFixed(0)}%</td>
              <td className={p.grievance > 0.3 ? 'warn' : ''}>{(p.grievance * 100).toFixed(0)}%</td>
              <td>{(p.cohesion * 100).toFixed(0)}%</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
