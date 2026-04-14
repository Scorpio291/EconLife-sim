import { useSimStore } from '../store';

function formatPop(n: number): string {
  if (n >= 1_000_000) return `${(n / 1_000_000).toFixed(1)}M`;
  if (n >= 1_000) return `${(n / 1_000).toFixed(0)}K`;
  return String(n);
}

export function ProvincePanel() {
  const state = useSimStore((s) => s.state);
  const sendAction = useSimStore((s) => s.sendAction);

  if (!state) return null;

  const { player, provinces } = state;
  const isTraveling = player.travel_status === 'in_transit';

  return (
    <div className="province-panel">
      <h3>Provinces</h3>
      <div className="province-list">
        {provinces.map((prov) => {
          const isCurrent = prov.id === player.province_id;
          return (
            <div
              key={prov.id}
              className={`province-card ${isCurrent ? 'current' : ''}`}
            >
              <div className="province-header">
                <span className="province-name">{prov.name}</span>
                {isCurrent && <span className="here-badge">HERE</span>}
              </div>
              <div className="province-stats">
                <span>Pop: {formatPop(prov.population)}</span>
                <span>Stability: {(prov.stability * 100).toFixed(0)}%</span>
                <span>Crime: {(prov.crime * 100).toFixed(0)}%</span>
                <span>Infra: {(prov.infrastructure * 100).toFixed(0)}%</span>
              </div>
              {!isCurrent && (
                <button
                  className="travel-btn"
                  disabled={isTraveling}
                  onClick={() => sendAction('travel', {
                    destination_province_id: prov.id,
                  })}
                >
                  {isTraveling ? 'In Transit...' : 'Travel'}
                </button>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
}
