import { useSimStore } from '../store';

function formatMoney(n: number): string {
  if (Math.abs(n) >= 1_000_000) return `$${(n / 1_000_000).toFixed(1)}M`;
  if (Math.abs(n) >= 1_000) return `$${(n / 1_000).toFixed(1)}K`;
  return `$${n.toFixed(0)}`;
}

export function BusinessPanel() {
  const state = useSimStore((s) => s.state);
  const sendAction = useSimStore((s) => s.sendAction);

  if (!state) return null;

  const { businesses, player, provinces } = state;

  const handleStartBusiness = () => {
    sendAction('start_business', {
      sector: 'retail',
      province_id: player.province_id,
    });
  };

  return (
    <div className="business-panel">
      <div className="panel-header">
        <h2>Your Businesses</h2>
        <button
          className="start-biz-btn"
          onClick={handleStartBusiness}
          disabled={player.travel_status === 'in_transit' || player.wealth < 10000}
          title={player.wealth < 10000 ? 'Need at least $10,000' : 'Start a retail business'}
        >
          + Start Business
        </button>
      </div>

      {businesses.length === 0 ? (
        <p className="empty-msg">No businesses owned yet</p>
      ) : (
        <div className="business-list">
          {businesses.map((biz) => {
            const province = provinces.find(p => p.id === biz.province_id);
            const profit = biz.revenue_per_tick - biz.cost_per_tick;
            return (
              <div key={biz.id} className="business-card">
                <div className="biz-header">
                  <span className="biz-sector">{biz.sector.replace(/_/g, ' ')}</span>
                  <span className="biz-location">{province?.name ?? `Province ${biz.province_id}`}</span>
                </div>
                <div className="biz-stats">
                  <div className="biz-stat">
                    <span className="biz-stat-label">Cash</span>
                    <span className="biz-stat-value">{formatMoney(biz.cash)}</span>
                  </div>
                  <div className="biz-stat">
                    <span className="biz-stat-label">Revenue/day</span>
                    <span className="biz-stat-value">{formatMoney(biz.revenue_per_tick)}</span>
                  </div>
                  <div className="biz-stat">
                    <span className="biz-stat-label">Cost/day</span>
                    <span className="biz-stat-value">{formatMoney(biz.cost_per_tick)}</span>
                  </div>
                  <div className="biz-stat">
                    <span className="biz-stat-label">Profit/day</span>
                    <span className={`biz-stat-value ${profit >= 0 ? 'positive' : 'negative'}`}>
                      {formatMoney(profit)}
                    </span>
                  </div>
                  <div className="biz-stat">
                    <span className="biz-stat-label">Quality</span>
                    <span className="biz-stat-value">{(biz.output_quality * 100).toFixed(0)}%</span>
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
