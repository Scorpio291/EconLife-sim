import { useSimStore } from '../store';

function formatMoney(n: number): string {
  if (Math.abs(n) >= 1_000_000) return `$${(n / 1_000_000).toFixed(1)}M`;
  if (Math.abs(n) >= 1_000) return `$${(n / 1_000).toFixed(1)}K`;
  return `$${n.toFixed(0)}`;
}

interface TopBarProps {
  showLog: boolean;
  onToggleLog: () => void;
}

export function TopBar({ showLog, onToggleLog }: TopBarProps) {
  const state = useSimStore((s) => s.state);
  const speed = useSimStore((s) => s.speed);
  const setSpeed = useSimStore((s) => s.setSpeed);

  if (!state) return null;

  const { player, date, tick } = state;

  return (
    <header className="top-bar">
      <div className="top-bar-left">
        <button
          className={`ctrl-btn ${showLog ? 'active' : ''}`}
          onClick={onToggleLog}
          title="Action Log"
        >
          Log
        </button>
        <span className="game-date">{date}</span>
        <span className="tick-count">Day {tick}</span>
      </div>

      <div className="top-bar-controls">
        <button
          className={`ctrl-btn ${speed === 'paused' ? 'active' : ''}`}
          onClick={() => setSpeed('paused')}
          title="Pause (Space)"
        >
          ||
        </button>
        <button
          className="ctrl-btn"
          onClick={() => setSpeed('step')}
          title="Step (Right Arrow)"
        >
          |&gt;
        </button>
        <button
          className={`ctrl-btn ${speed === 'play' ? 'active' : ''}`}
          onClick={() => setSpeed('play')}
          title="Play (Space)"
        >
          &gt;
        </button>
        <button
          className={`ctrl-btn ${speed === 'fast' ? 'active' : ''}`}
          onClick={() => setSpeed('fast')}
          title="Fast Forward (Up Arrow)"
        >
          &gt;&gt;
        </button>
      </div>

      <div className="top-bar-right">
        <div className="player-stat">
          <span className="stat-label">Wealth</span>
          <span className="stat-value">{formatMoney(player.wealth)}</span>
        </div>
        <div className="player-stat">
          <span className="stat-label">Health</span>
          <div className="health-bar">
            <div
              className="health-fill"
              style={{ width: `${player.health * 100}%` }}
            />
          </div>
        </div>
        <div className="player-stat">
          <span className="stat-label">Location</span>
          <span className={`stat-value ${player.travel_status === 'in_transit' ? 'in-transit' : ''}`}>
            {state.provinces.find(p => p.id === player.province_id)?.name ?? `Province ${player.province_id}`}
            {player.travel_status === 'in_transit' && ' (traveling)'}
          </span>
        </div>
      </div>
    </header>
  );
}
